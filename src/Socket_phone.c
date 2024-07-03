#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <termios.h>
#include <fcntl.h>
#include <assert.h>
#include <complex.h>
#include <math.h>
#include <string.h>
#include <sys/select.h>
#include <sys/time.h>

#define DATA_SIZE 1024
#define FFT_SLIDE 3
#define max_call 3
#define NOISE_CANCEL_SLIDE 5

// 通話の開始
int connected = 0;
// ミュートの開始
int mute = 0;
// 消音
int silence = 0;
//　ボイスチェンジャー
int VC = 0;
// ringスレッドが終了したかどうか
int ring_finished = 0;

typedef short sample_t;

/* 標本(整数)を複素数へ変換 */
void sample_to_complex(sample_t * s, 
		       complex double * X, 
		       long n) {
  long i;
  for (i = 0; i < n; i++) X[i] = s[i];
}

/* 複素数を標本(整数)へ変換. 虚数部分は無視 */
void complex_to_sample(complex double * X, 
		       sample_t * s, 
		       long n) {
  long i;
  for (i = 0; i < n; i++) {
    s[i] = creal(X[i]);
  }
}

/* 高速(逆)フーリエ変換;
   w は1のn乗根.
   フーリエ変換の場合   偏角 -2 pi / n
   逆フーリエ変換の場合 偏角  2 pi / n
   xが入力でyが出力.
   xも破壊される
 */
void fft_r(complex double * x, 
	   complex double * y, 
	   long n, 
	   complex double w) {
  if (n == 1) { y[0] = x[0]; }
  else {
    complex double W = 1.0; 
    long i;
    for (i = 0; i < n/2; i++) {
      y[i]     =     (x[i] + x[i+n/2]); /* 偶数行 */
      y[i+n/2] = W * (x[i] - x[i+n/2]); /* 奇数行 */
      W *= w;
    }
    fft_r(y,     x,     n/2, w * w);
    fft_r(y+n/2, x+n/2, n/2, w * w);
    for (i = 0; i < n/2; i++) {
      y[2*i]   = x[i];
      y[2*i+1] = x[i+n/2];
    }
  }
}

void fft(complex double * x, 
	 complex double * y, 
	 long n) {
  long i;
  double arg = 2.0 * M_PI / n;
  complex double w = cos(arg) - 1.0j * sin(arg);
  fft_r(x, y, n, w);
  for (i = 0; i < n; i++) y[i] /= n;
}

void ifft(complex double * y, 
	  complex double * x, 
	  long n) {
  double arg = 2.0 * M_PI / n;
  complex double w = cos(arg) + 1.0j * sin(arg);
  fft_r(y, x, n, w);
}



void *send_data(void *arg) {
    FILE *fp;
    char *cmdline = "rec -t raw -b 16 -c 1 -e s -r 44100 -";
    if((fp = popen(cmdline, "r")) == NULL){
        perror("popen");
        exit(1);
    }

    int s = *(int *)arg;
    short data[DATA_SIZE];
    while(1){
        int nnn = fread(data, sizeof(short), DATA_SIZE, fp);
        if(nnn == -1){
            perror("read");
            exit(1);
        }
        if(nnn == 0){
            break;
        }
        if(mute == 1){
            short mute_sig[DATA_SIZE];
            memset(mute_sig, 0, DATA_SIZE);
            int m = send(s, mute_sig, sizeof(mute_sig), 0);
            if (m == -1) {
                perror("write");
                exit(1);
            }
            continue;
        }

        long n = DATA_SIZE;
        complex double * X = calloc(sizeof(complex double), n);
        complex double * Y = calloc(sizeof(complex double), n);
        /* 複素数の配列に変換 */
        sample_to_complex(data, X, n);
        /* FFT -> Y */
        fft(X, Y, n);

        if (VC == 1){
            // FFT_SLIDEだけずらす
            for(int i = (int)n/2 - 1 - FFT_SLIDE; i >= 0; --i){
                Y[i+FFT_SLIDE] = Y[i];
            }
            for(int i = 0; i< FFT_SLIDE; ++i){
                Y[i] = 0;
            }
        }

        // LPFでノイズカット
        for(int i = 0; i< NOISE_CANCEL_SLIDE; ++i){
                Y[i] = 0;
        }

        /* IFFT -> Z */
        ifft(Y, X, n);
        /* 標本の配列に変換 */
        complex_to_sample(X, data, n);

        free(X);
        free(Y);

        int nn = send(s, data, sizeof(data), 0);
        if(nn < 0){
            perror("send");
            exit(1);
        }
    }
    pclose(fp);
    return NULL;
}

void *recv_data(void *arg) {
    FILE *fp;
    char *cmdline = "play -t raw -b 16 -c 1 -e s -r 44100 -";
    if((fp = popen(cmdline, "w")) == NULL){
        perror("popen");
        exit(1);
    }

    int s = *(int *)arg;
    short data[DATA_SIZE];
    while (1) {
        int n = recv(s, data, sizeof(data), 0);
        if (n == -1) {
            perror("recv");
            exit(1);
        } else if (n == 0) {
            break;
        }

        if (silence == 1){
            short silence_sig[DATA_SIZE];
            memset(silence_sig, 0, DATA_SIZE);
            int m = fwrite(silence_sig, sizeof(short), DATA_SIZE, fp);
            if (m == -1) {
                perror("write");
                exit(1);
            }
            continue;
        }

        int m = fwrite(data, sizeof(short), DATA_SIZE, fp);
        if (m == -1) {
            perror("write");
            exit(1);
        }
    }
    pclose(fp);
    return NULL;
}

void *ring(void *arg){
    int counter = 0;
    while(counter < max_call){
        if(connected == 1){
            break;
        }
        FILE *fp;
        char *cmdline = "play ../data/Ringtone/call.mp3";
        if((fp = popen(cmdline, "w")) == NULL){
            perror("popen");
            exit(1);
        }
        ++counter;
        pclose(fp);
    }

    pthread_exit(NULL);
}

void *ring(){
    int counter = 0;
    while(counter < max_call){
        if(connected == 1){
            break;
        }
        FILE *fp;
        char *cmdline = "play ../data/Ringtone/call.mp3";
        if((fp = popen(cmdline, "w")) == NULL){
            perror("popen");
            exit(1);
        }
        ++counter;
        pclose(fp);
    }

    // 電話をとらずに終了した
    ring_finished = 1;

    pthread_exit(NULL);
}

void *voicemail(){
    FILE *fp;
    char *cmdline = "play ../data/Ringtone/voicemail.wav";
    if((fp = popen(cmdline, "w")) == NULL){
        perror("popen");
        exit(1);
    }

    // 留守番音声の後にビープ音が流れるように
    sleep(9);

    char *cmdline2 = "play ../data/Ringtone/beep.wav";
    if((fp = popen(cmdline2, "w")) == NULL){
        perror("popen");
        exit(1);
    }

    pclose(fp);
    pthread_exit(NULL);
}

void *record_voicemail(void *arg){
    char *cmdline = "sox -t raw -b 16 -c 1 -e s -r 44100 - ../voicemail/voicemail.wav";
    FILE *fp = popen(cmdline, "w");
    if (fp == NULL) {
        perror ("popen recv failed");
        exit(EXIT_FAILURE);
    }

    int s = *(int *)arg;
    short data[DATA_SIZE];
    while (1) {
        int n = recv(s, data, sizeof(data), 0);
        if (n == -1) {
            perror("recv");
            exit(1);
        } else if (n == 0) {
            break;
        }

        int m = fwrite(data, sizeof(short), DATA_SIZE, fp);
        if (m == -1) {
            perror("write");
            exit(1);
        }
    }
    pclose(fp);
    return NULL;
}



// getcharはEOF(Enter)押さないと発動しない
// cでserver側が通話開始、mでお互いに相手をミュート
void *getchar_self(void *arg){
    int s = *(int *)arg;
    char data[1];
    while(1){
        data[0] = getchar();
        switch(data[0]){
            case 'c':
                connected = 1;
                // 送る処理
                int send_char_num = send(s, data, sizeof(char), 0);
                if(send_char_num < 0){
                    perror("send");
                    exit(1);
                }
                pthread_exit(NULL);
            case 'm':
                mute = (mute + 1) % 2;
                break;
            case 's':
                silence = (silence + 1) % 2;        
                break;        
            case 'v':
                VC = (VC + 1) % 2;
                break;
        }
    }
}

// リングスレッドが終了したら自分も終了
// タイムアウトとselectを用いて、入力があった時のみgetcharに移る
void *get_call(void *arg){
    int s = *(int *)arg;
    char data[1];
    fd_set readfds;
    struct timeval time;
    // ringスレッドが終わっていない間
    while(!ring_finished){
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);

        time.tv_sec = 0;
        time.tv_usec = 100000;

        int ret = select(STDIN_FILENO + 1, &readfds, NULL, NULL, &time);

        if (ret == -1){
            perror("select");
            exit(1);
        }
        else if (ret == 0){
            // タイムアウト
            continue;
        }
        else{
            data[0] = getchar();
            switch(data[0]){
                case 'c':
                    connected = 1;
                    // 送る処理
                    int send_char_num = send(s, data, sizeof(char), 0);
                    if(send_char_num < 0){
                        perror("send");
                        exit(1);
                    }
                    pthread_exit(NULL);
            }
         }

    }   

    pthread_exit(NULL);
}

// cでclient側が通話開始
void *getchar_opponent(void *arg){
    int s = *(int *)arg;
    char data[1];
    fd_set readfds;
    struct timeval time;
    while (!ring_finished ) {
        FD_ZERO(&readfds);
        FD_SET(s, &readfds); //STDINではなくソケットを監視
 
        time.tv_sec = 0;
        time.tv_usec = 100000;

        int ret = select(s + 1, &readfds, NULL, NULL, &time);

        if (ret == -1){
            perror("select");
            exit(1);
        }
        else if (ret == 0){
            // タイムアウト
            continue;
        }
        else{
        int n = recv(s, data, sizeof(data), 0);
        if (n == -1) {
            perror("recv");
            exit(1);
        }
        switch(data[0]){
            case 'c':
                connected = 1;
                pthread_exit(NULL);
        }
        }
    }
    pthread_exit(NULL);
}


int main(int argc, char *argv[]){
    // サーバー側
    if (argc == 2) {
        // ソケットの作成．返り値はファイルディスクリプタ
        int ss = socket(PF_INET, SOCK_STREAM, 0);
        if(ss == -1){
            perror("socket");
            exit(1);
        }
        struct sockaddr_in addr; //最終的にbindに渡すアドレス
        addr.sin_family = AF_INET; /* これはIPv4のアドレス*/
        addr.sin_port = htons(atoi(argv[1]));  //ポート番号
        addr.sin_addr.s_addr = INADDR_ANY; //どのIPアドレスも受付

        // どのポートで待ち受けるか
        int ret = bind(ss, (struct sockaddr *)&addr, sizeof(addr));
        if(ret == -1){
            perror("bind");
            exit(1);
        }
        
        // 待ち受け可能宣言
        listen(ss, 10);

        struct sockaddr_in client_addr;
        socklen_t len = sizeof(struct sockaddr_in);
        int s = accept(ss, (struct sockaddr *)&client_addr, &len);
        if(s == -1){
            perror("accept");
            exit(1);
        }
        close(ss);

        // 着信音
        pthread_t get_call_thread;
        pthread_t ring_thread;

        if (pthread_create(&get_call_thread, NULL, get_call, &s) != 0){
            perror("pthread_create");
            exit(1);
        }

        if (pthread_create(&ring_thread, NULL, ring, &s) != 0){
            perror("pthread_create");
            exit(1);
        }

        pthread_join(get_call_thread, NULL);
        pthread_join(ring_thread, NULL);

        // 電話をとらなかったら
        if (connected == 0){
            // recvしてそれをファイルにリダイレクト(wav)
            pthread_t record_voicemail_thread;
            if (pthread_create(&record_voicemail_thread, NULL, record_voicemail, &s) != 0) {
                perror("pthread_create");
                exit(1);
            }

            pthread_join(record_voicemail_thread, NULL);

            close(s);
            return 0;
        }

        // 通話開始
        pthread_t send_thread, recv_thread, getchar_self_thread;

        if (pthread_create(&send_thread, NULL, send_data, &s) != 0) {
            perror("pthread_create");
            exit(1);
        }

        if (pthread_create(&recv_thread, NULL, recv_data, &s) != 0) {
            perror("pthread_create");
            exit(1);
        }

        if (pthread_create(&getchar_self_thread, NULL, getchar_self, &s) != 0){
            perror("pthread_create");
            exit(1);
        }

        pthread_join(send_thread, NULL);
        pthread_join(recv_thread, NULL);
        pthread_join(getchar_self_thread, NULL);

        close(s);
    }

    else if(argc == 3){
        // クライアント側
        int s = socket(PF_INET, SOCK_STREAM, 0);
        if (s == -1) {
            perror("socket");
            exit(1);
        }

        struct sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = inet_addr(argv[1]);
        addr.sin_port = htons(atoi(argv[2]));

        int ret = connect(s, (struct sockaddr *)&addr, sizeof(addr));
        if (ret == -1) {
            perror("connect");
            exit(1);
        }

        // 発信
        pthread_t ring_thread, getchar_opponent_thread;

        // serv側のcをクライアント側が確実に受け取れるようにタイミングをずらす
        sleep(1);

        if (pthread_create(&ring_thread, NULL, ring, &s) != 0) {
            perror("pthread_create");
            exit(1);
        }

        if (pthread_create(&getchar_opponent_thread, NULL, getchar_opponent, &s) != 0) {
            perror("pthread_create");
            exit(1);
        }

        pthread_join(ring_thread, NULL);
        pthread_join(getchar_opponent_thread, NULL);

        // 電話が取られなかったら
        if(connected == 0){
            // 留守電音声流す
            // sendで自分の声を送る
            pthread_t voicemail_thread, send_thread;
            if (pthread_create(&voicemail_thread, NULL, voicemail, &s) != 0) {
                perror("pthread_create");
                exit(1);
            }

            if (pthread_create(&send_thread, NULL, send_data, &s) != 0) {
                perror("pthread_create");
                exit(1);
            }   

            pthread_join(voicemail_thread, NULL);
            pthread_join(send_thread, NULL);

            close(s);
            return 0;
        } 

        // 通話開始
        pthread_t send_thread, recv_thread, getchar_self_thread;

        if (pthread_create(&send_thread, NULL, send_data, &s) != 0) {
            perror("pthread_create");
            exit(1);
        }

        if (pthread_create(&recv_thread, NULL, recv_data, &s) != 0) {
            perror("pthread_create");
            exit(1);
        }

        if (pthread_create(&getchar_self_thread, NULL, getchar_self, &s) != 0){
            perror("pthread_create");
            exit(1);
        }

        pthread_join(send_thread, NULL);
        pthread_join(recv_thread, NULL);
        pthread_join(getchar_self_thread, NULL);

        close(s);
    }

    // エラー処理
    else{
        fprintf(stderr, "Usage (server): %s <Port>\n", argv[0]);
        fprintf(stderr, "Usage (client): %s <IP> <Port>\n", argv[0]);
        exit(1);
    }
    return 0;
}
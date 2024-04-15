#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <stdbool.h>
#include <sys/shm.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/wait.h>
#include <time.h>
#include <netdb.h>
#include <signal.h>

#define MAX_OPCJA 30

// Struktura zarządzająca informacjami gracza
struct gra
{

    char opcja[MAX_OPCJA];
    char wiadomosc[1024]; //Przewidziano dla liczby, używane dla wiadomości
    int liczba; //Losowa liczba przesyłana w celu uniknięcia problemów z seed
    char nick[25]; //Limit długości nicku dla lepszej czytelności

};

//Struktura dla danych w pamięci współdzielonej
struct dzielona
{
    int pkt[2]; //Punkty graczy: pkt[0] dla gracza 1, pkt[1] dla gracza 2
    int wynik_dodawania;
    int tura;
    int stan_gry;
    int liczba_losowa;

};
//Handler sygnału SIGCHLD do czyszczenia procesów zombie
void handle_sigchld(int sig)
{
    int status;
    pid_t pid;
    while((pid = waitpid(-1, &status, WNOHANG)) > 0)
    {
        printf("Child process %d terminated\n", pid);
    }
}

int main(int argc, char *argv[])
{

    printf("WARIANT A\n");
    pid_t child_pid;
    struct gra gr1, gr2; //gra 1 odpowiada za start gry a gra 2 za pozniejsze ogarnianie co sie dzieje
    struct sockaddr_in moj_adres, adres_rywala, *cel;		/* struktury adresowe */
    struct addrinfo *info_rywal;
    socklen_t adr_len;
    int sockfd;
    int player=0;

    struct sigaction sa;
    sa.sa_handler = handle_sigchld;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa, NULL) == -1)   //obsługa synagłu dziecka
    {
        perror("sigaction");
        exit(1);
    }

    if(argc<3 || argc>4)
    {
        printf("Podano niewlasciwa liczbe argumentow!\n");
        printf("Sprobuj ponownie\n");
        return 1;
    }

    if(argv[3])
    {
        if(strlen(argv[3]) > 25)
        {
            printf("Twoj nick jest za dlugi! (max 25znakow)\n");
        }
        else strcpy(gr1.nick, argv[3]);
    }

    int shmid = shmget(IPC_PRIVATE, sizeof(struct dzielona), IPC_CREAT | 0666);
    struct dzielona *dz = (struct dzielona *) shmat(shmid, 0, 0);

    if(getaddrinfo(argv[1], argv[2], NULL, &info_rywal) != 0)
    {
        printf("Nie udalo sie polaczyc z maszyna %s.\n", argv[1]);
        return 1;
    }

    // Utworzenie gniazda UDP
    if((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    {
        printf("Nie udalo sie utworzyc gniazda UDP.\n");
        return 1;
    }

    // Uzupelnienie struktury moj_adres i adres_rywala

    moj_adres.sin_family = AF_INET;
    moj_adres.sin_addr.s_addr = htonl(INADDR_ANY);
    moj_adres.sin_port = htons(atoi(argv[2]));

    cel = (struct sockaddr_in *)(info_rywal->ai_addr);
    adres_rywala.sin_family = AF_INET;
    adres_rywala.sin_addr = cel->sin_addr;
    adres_rywala.sin_port = htons(atoi(argv[2]));
    adr_len = sizeof(adres_rywala);
    // Powiazanie gniazda z adresem maszyny
    if(bind(sockfd, (struct sockaddr *)&moj_adres, sizeof(moj_adres)) < 0) //bindowanie
    {
        printf("Nie udalo sie powiazac gniazda z adresem Twojej maszyny.\n");
        close(sockfd);
        return 1;
    }

    dz->tura = 1;  //przypisuje tym zmiennym wartość 1(inaczej nie zadziała przez to ze wysyłam testową wiadomość)
    dz->stan_gry = 1;
    dz->pkt[0] = 0;
    dz->pkt[1] = 0;

    printf("Rozpoczynam gre z %s.\n", inet_ntoa(cel->sin_addr));
    printf("* Napisz koniec, aby zakonczyc\n");
    printf("* Napisz wynik, aby wystwietlic aktualny wynik gry\n\n");
    printf("[Propozycja gry wyslana.]\n");

    strcpy(gr1.opcja, "start");
    if(sendto(sockfd, &gr1, sizeof(gr1), 0, (struct sockaddr *)&adres_rywala, sizeof(adres_rywala)) < 0)
    {

        printf("Nie udalo sie wyslac danych.\n");
        close(sockfd);
        return 1;
    }
    strcpy(gr1.opcja, "nie");
    child_pid = fork();

    dz->wynik_dodawania=0;

    if (child_pid < 0)   //Pojawił się błąd  (tylko proces rodzica)
    {
        perror("Blad:\n");
    }
    else if (child_pid == 0)//proces potomny
    {
        while(1)
        {

            if(recvfrom(sockfd, &gr2, sizeof(gr2), 0, (struct sockaddr *)&adres_rywala, &adr_len) > 0)  //odbieranie wiadomosci
            {

                dz->tura =0;
                (dz->wynik_dodawania) =gr2.liczba;


            }
            else
            {
                printf("Nie udalo sie odebrac danych.\n");
                close(sockfd);
                return 1;
            }
            if(strcmp(gr2.opcja, "start") == 0)
            {
                srand(time(NULL));   //inicjalizacja ziarna
                dz->liczba_losowa = (rand()%10)+1;      //licza losowa na starcie
                dz->wynik_dodawania+=dz->liczba_losowa;
                printf("\n%s) dolaczyl do gry\n",inet_ntoa(adres_rywala.sin_addr));
                dz->stan_gry = 1;
                dz->pkt[0] = 0;
                dz->pkt[1] = 0;

                printf("Losowa wartosc początkowa: %d podaj kolejna liczbe..\n",dz->liczba_losowa);

            }
            else if(strcmp(gr2.wiadomosc, "koniec\n") == 0)
            {
                printf("Drugi gracz zakonczył gre, czekam na innego gracza...\n");
                dz->stan_gry = 0;

            }
            else
            {
                if(dz->stan_gry == 1)
                {
                    //Wypisanie odebranej wiadomości
                    if(strcmp(gr2.nick, "") == 0)
                    {

                        printf("%s: wybrał liczbe: %d\n", inet_ntoa(cel->sin_addr),dz->wynik_dodawania);
                        printf("Twoja tura\n");
                        if((dz->wynik_dodawania) == 50 )
                        {
                            printf("\n\n\nprzegrales\n");
                            printf("rozpoczynam nową rozgrywkę...\n");
                            dz->wynik_dodawania=0;
                            dz->liczba_losowa = (rand()%10)+1;      // loswa liczba
                            dz->wynik_dodawania+=dz->liczba_losowa;
                            printf("liczba poczatkowa: %d\n", dz->liczba_losowa);
                            printf("Twoja tura\n");
                            dz->pkt[1]++;
                        }
                    }
                    else
                    {
                        printf("%s: wybrał liczbe: %d\n",gr2.nick,dz->wynik_dodawania);
                        printf("Twoja tura\n");
                        if((dz->wynik_dodawania) == 50 )
                        {
                            printf("\n\n\nprzegrales\n");
                            printf("rozpoczynam nową rozgrywkę...\n");
                            dz->wynik_dodawania=0;
                            dz->liczba_losowa = (rand()%10)+1;      //losowa liczba
                            dz->wynik_dodawania+=dz->liczba_losowa;
                            printf("liczba poczatkowa: %d\n", dz->liczba_losowa);
                            printf("Twoja tura\n");
                            dz->pkt[1]++;
                        }
                    }
                }
            }
        }
        exit(0);
    }
    else     //proces rodzica
    {
        while(1)
        {
            memset(gr1.wiadomosc, 0, sizeof(gr1.wiadomosc)); //zeruje tablice
            fgets(gr1.wiadomosc, sizeof(gr1.wiadomosc), stdin); //pobiera wiadomosc
            int x = atoi(gr1.wiadomosc);
            if(x !=0)
            {
                if(dz->tura == player)
                {


                    if(x > (dz->wynik_dodawania) && x <= (dz->wynik_dodawania + 10))  //warunek przedzialowy
                    {

                        (dz->wynik_dodawania) =x;
                        if((dz->wynik_dodawania) == 50 )
                        {

                            printf("\n\n\nWYGRALES\n");
                            printf("rozpoczynam nową rozgrywkę... tura przeciwnika\n");
                            dz->pkt[0]++;

                        }
                        gr1.liczba=(dz->wynik_dodawania);
                        if(sendto(sockfd, &gr1, sizeof(gr1), 0, (struct sockaddr *)&adres_rywala, sizeof(adres_rywala)) < 0)  //wysylanie
                        {
                            printf("Nie udalo sie wyslac danych.\n");
                            close(sockfd);
                            return 1;

                        }
                        dz->tura = (player == 1) ? 2 : 1; //zmiana tur
                    }
                    else printf("takiej liczby nie mozesz dodac\n");

                }
                else printf("Tura przeciwnika czekaj na swoja kolej\n");

            }
            else
            {
                if(strcmp(gr1.wiadomosc, "koniec\n") == 0) //koniczenie rozgrywki
                {
                    dz->wynik_dodawania=(rand() % 10) + 1;
                    sendto(sockfd, &gr1, sizeof(gr1), 0, (struct sockaddr *)&adres_rywala, sizeof(adres_rywala));
                    kill(child_pid, SIGTERM);
                    int status;
                    waitpid(child_pid, &status, 0);
                    shmdt(dz);
                    shmctl(shmid, IPC_RMID, 0);
                    close(sockfd);
                    break;
                }
                else if(strcmp(gr1.wiadomosc, "wynik\n") == 0)   //wynik
                {


                    printf("TY %d:%d RYWAL\n",dz->pkt[0], dz->pkt[1]);


                }
                else printf("bledne dane\n");
            }

        }
    }
    shmdt(dz);
    shmctl(shmid, IPC_RMID, 0);
    close(sockfd);
    return 0;
}


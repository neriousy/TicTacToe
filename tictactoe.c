#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>	
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <sys/sem.h> 
#include <signal.h>

void mysyserr(char *mymsg); //funkcja do wyswietlania bledow
void siginthandle(int signal); //funkcja do obslugi sygnalu ctrl + c
void reset_game(); // funkcja do resetowania planszy
void show_game(); // funkcja do pokazywania planszy
void reset_score(); // funkcja do zerowania wyniku gry
int check_game(); // funkcja sprawdzajaca czy gra dobiegla konca 


struct sockaddr_in *self_ip, *other_ip, self_struct, other_struct;
struct addrinfo *self_info, *other_info;

key_t shmkey; // klucz pamieci wspoldzielonej
int   shmid; // id pamieci wspoldzielonej

struct my_data { // struktura ktora wysylamy do pamieci wspoldzielonej 
	char opponentNickname[20];
	int myTurn;
	int pola[9];
	int znak;
	int score[2];
} *shared_data;



int main(int argc, char *argv[]) {
	int sockfd, pid;
	char pole[10], nickname[20];
	char opponentAddress[INET_ADDRSTRLEN];
	signal(SIGINT, siginthandle); // obsluga sygnalu


	if(argc == 1 ||argc > 3){ // obsluga argumentow
		printf("Zla ilosc argumentow\nProgram uruchamia sie z maksymalnie dwoma argumentami. \nPierwszy: nazwa hosta \nDrugi (opcjonalny): nickname\n\n");
		return 0;
	}else if(argc == 2){
		strcpy(nickname, "NN");	
	}else if(argc == 3){
		strcpy(nickname, argv[2]);
	}

	if(getaddrinfo(argv[1], "9988", NULL, &other_info) != 0){ // uzupelnianie struktur addrinfo
		printf("Podany host jest poza zasiegiem \n");
		mysyserr("getaddrinfo");
		exit(EXIT_FAILURE);
	}

	if(getaddrinfo(NULL, "9988", NULL, &self_info) != 0){
		mysyserr("getaddrinfo");
		exit(EXIT_FAILURE);
	}

	if((shmkey = ftok(argv[0], 1)) == -1){ // generowanie klucza pamieci wspoldzielonej
		mysyserr("ftok\n");
		exit(1);
	}
	if((shmid = shmget(shmkey, sizeof(struct my_data), 0600 | IPC_CREAT)) == -1){ // tworzenie segmentu pamieci wspoldzielonej
		mysyserr("shmget");
		exit(1);
	}



	self_ip = (struct sockaddr_in *)(self_info->ai_addr); // pobieranie adresu ze struktur addrinfo
	other_ip = (struct sockaddr_in *)(other_info->ai_addr);

	self_struct.sin_family = AF_INET;
	self_struct.sin_addr = self_ip->sin_addr;
	self_struct.sin_port = htons(9988);

	other_struct.sin_family = AF_INET;
	other_struct.sin_addr = other_ip->sin_addr;	
	other_struct.sin_port = htons(9988);

	sockfd = socket(AF_INET, SOCK_DGRAM, 0); // tworzenie socketu

	inet_ntop(AF_INET, &(other_ip->sin_addr), opponentAddress, INET_ADDRSTRLEN); // tlumaczenie adresu przeciwnika

	if(bind(sockfd, (struct sockaddr *)&self_struct, sizeof(self_struct)) == -1){ // bindowanie socketu do swojego adresu
		mysyserr("bind");
		exit(EXIT_FAILURE);
	}

	shared_data = (struct my_data *) shmat(shmid, (void *)0, 0);  // podlaczenie pamieci wspoldzielonej
	strcpy(pole, "s");

	reset_game(shared_data); // resetowanie gry

	
	if(shared_data == (struct my_data *) -1){
		mysyserr("shared_data");
	}

	if(sendto(sockfd, &pole, sizeof(pole), 0, (struct sockaddr *)&other_struct, sizeof(other_struct)) < 0){ // wysylanie pakietu s - start gry
		mysyserr("sendto");
		close(sockfd);
		exit(EXIT_FAILURE);
	}

	if(sendto(sockfd, &nickname, sizeof(nickname), 0, (struct sockaddr *)&other_struct, sizeof(other_struct)) < 0){ // wysylanie pakietu ze swoja nazwa 
		mysyserr("sendto");
		close(sockfd);
		exit(EXIT_FAILURE);
	}



	printf("Rozpoczynam gre z %s. Napisz <koniec> by zakonczyc.\n", opponentAddress);
	fflush(stdout);

	if((pid = fork()) == 0){
		connect(sockfd, (struct sockaddr *)&other_struct, sizeof(other_struct)); // laczenie sie z adresem przeciwnika
		char temp;
		while(1){
			fflush(stdout);
			read(sockfd, &pole, sizeof(pole));
			if(strcmp(pole, "\n") == 0){

			}else if(strcmp(pole, "s") == 0){ // s - start gry
				read(sockfd, shared_data->opponentNickname, sizeof(shared_data->opponentNickname)); // pobieranie adresu przeciwnika
				shared_data->myTurn = 1;
				shared_data->znak = 1;
				printf("%s (%s) dolaczyl do gry]\n", shared_data->opponentNickname, opponentAddress);
				reset_game();
				show_game();
				reset_score();
				printf("[wybierz pole] ");
				strcpy(pole, "x"); // x - wysylanie przeciwnikowi swojej nazwy
				write(sockfd, &pole, sizeof(pole));
				write(sockfd, &nickname, sizeof(nickname));
				shared_data->score[0] = 0;
				shared_data->score[1] = 0;
				fflush(stdout);
			}else if(strcmp(pole, "x") == 0){ // pobieranie nazwy przeciwnika
				read(sockfd, shared_data->opponentNickname, sizeof(shared_data->opponentNickname));
				shared_data->myTurn = 0;
				shared_data->znak = 2;
				reset_game();
				reset_score();
				printf("%s (%s) dolaczyl do gry]\n", shared_data->opponentNickname, opponentAddress);
				fflush(stdout);
			}
			 else if(strcmp(pole, "<koniec>") == 0){ // obsluga komendy koniec od przeciwnika
				printf("[%s (%s) zakonczyl gre, mozesz poczekac na kolenego gracza]\n", shared_data->opponentNickname, opponentAddress);
				fflush(stdout);
			} else {
				printf("[%s (%s) wybral pole %s] \n", shared_data->opponentNickname, opponentAddress, pole); // wyswietlenie informacji o ruchu przeciwnika
				temp = pole[0];
				shared_data->pola[temp - 97] = 3 - shared_data->znak; // aktualizacja pola
				show_game(); // wyswietlanie planszy
				if(check_game() == 1){ // sprawdzanie stanu gry
					printf("[Pregrana! Zagraj jeszcze raz]\n");
					shared_data->score[1]++;
					reset_game();
					show_game();
					fflush(stdout);
				}
				if(check_game() == 2){
					printf("Remis!\n");
					reset_game();
					show_game();
					fflush(stdout);
				}
				printf("[wybierz pole] ");
				shared_data->myTurn = 1;
				fflush(stdout);
			}
		}	
	} else{
		connect(sockfd, (struct sockaddr *)&other_struct, sizeof(other_struct)); // podlaczenie pod adres przeciwnika 
		printf("[Propozycja gry wyslana]\n");
		fflush(stdout);
		char temp;
		while(1){
			fgets(pole, 10, stdin);
			pole[strlen(pole) -1] = '\0';
			temp = pole[0];
			if(strcmp(pole, "<wynik>") == 0){ // obsluga komendy wynik
				printf("Ty %d : %d %s\n", shared_data->score[0], shared_data->score[1], shared_data->opponentNickname);
				printf("[wybierz pole] ");
				fflush(stdout);
			} else if (strcmp(pole, "<koniec>") == 0){ // obsluga komendy koniec - zamkniecie gry
				fflush(stdout);
				write(sockfd, &pole, sizeof(pole)); // wyslanie informacji przeciwnikowi o zakonczeniu rozgrywki
				kill(pid, SIGINT);
				close(sockfd);
				exit(0);
			}  else{
				if(shared_data->myTurn){
					while(shared_data->pola[temp - 97] != 0 || temp < 97 || temp > 105 || temp == 'x' || temp == 's'){ // obsluga niepoprawnych pol / komend
						if(strcmp(pole, "<koniec>") == 0){

							write(sockfd, &pole, sizeof(pole));
							kill(pid, SIGINT);
							close(sockfd);
							exit(0);
						} else{
							printf("Prosze wybrac inne pole\n");
							printf("[wybierz pole] ");
							fgets(pole, 10, stdin);
							pole[strlen(pole) -1] = '\0';
							temp = pole[0];
							fflush(stdout);
						}
						
					}
					printf("wybrales %s \n", pole);
					write(sockfd, &pole, sizeof(pole));
					shared_data->myTurn = 0;
					shared_data->pola[temp - 97] += shared_data->znak; // aktualizacja pola
					if(check_game() == 1){
						printf("[Wygrana! Kolejna rozgrywka, poczekaj na swoja kolej]\n"); // wyswietlenie informacji o wyniku gry
						shared_data->score[0]++;
						reset_game();
						show_game();
						fflush(stdout);
					}
					if(check_game() == 2){
						printf("Remis! Kolejna arozgrywka, poczekaj na swoja kolej]\n");
						reset_game();
						show_game();
						fflush(stdout);
					}
				}else{
					printf("Poczekaj na swoja kolej\n");
					fflush(stdout);
				}
			}
			fflush(stdout);
		}
	}	
    return 0;
}


void mysyserr(char *mymsg){ // funkcja do wyswietlkania bledow
	printf("ERROR: %s (errno: %d, %s)\n", mymsg, errno, strerror(errno));
}

void siginthandle(int signal) { // obsluga Ctrl^C / zamkniecie programu
		if(shmdt(shared_data) != 0){ // odlaczenie pamieci podrecznej
			mysyserr("shmdt");  
		}
		if(shmctl(shmid, IPC_RMID, 0) != 0){ // usuniecie pamieci podrecznej
			mysyserr("shmctl");
		}
		exit(0);
}

void reset_game(){ // resetowanie planszy gry
	int i;
	for(i = 0; i < 9; i++){
		shared_data->pola[i] = 0;
	}
}

void reset_score(){ // resetowanie wyniku gry
	shared_data->score[0] = 0;
	shared_data->score[1] = 0;
}

void show_game(){ // wyswietlenie planszy gry
	int i;
	char start = 'a';
	char opponent, me;
	if(shared_data->znak == 1){
		opponent = 'O'; me = 'X';
	} else{
		opponent = 'X'; me = 'O';
	}

	for(i = 0; i < 9; i += 3){
		if(shared_data->pola[i] == 0){
			printf("%c |", start + i);
		} else if(shared_data->pola[i] == shared_data->znak){
			printf("%c |", me);
		} else{
			printf("%c |", opponent);
		}

		if(shared_data->pola[i + 1] == 0){
			printf(" %c |", start + i + 1);
		} else if(shared_data->pola[i + 1] == shared_data->znak){
			printf(" %c |", me);
		} else {
			printf(" %c |", opponent);
		}

		if(shared_data->pola[i + 2] == 0){
			printf(" %c \n", start + i + 2);
		} else if(shared_data->pola[i + 2] == shared_data->znak){
			printf(" %c \n", me);
		} else{
			printf(" %c \n", opponent);
		}
	}
}

int check_game(){ // sprawdzenie wyniku gry
	int i;
	for(i = 0; i < 3; i ++){
		if((shared_data->pola[i] == shared_data->pola[i + 1]) && (shared_data->pola[i + 1] == shared_data->pola[i + 2]) && (shared_data->pola[i] != 0)){ // kombinacje poziome
			return 1;
		}
		if((shared_data->pola[i] == shared_data->pola[i + 3]) && shared_data->pola[i + 3] == shared_data->pola[i + 6] && (shared_data->pola[i] != 0)){ // kombinacje pionowe
			return 1;
		}
	}

	if((shared_data->pola[0] == shared_data->pola[4]) && (shared_data->pola[4] == shared_data->pola[8]) && (shared_data->pola[4] != 0)){ // kombinacje ukosne
		return 1;
	}

	if((shared_data->pola[2] == shared_data->pola[4]) && (shared_data->pola[4] == shared_data->pola[6]) && (shared_data->pola[4] != 0)){
		return 1;
	}

	for(i = 0; i < 9; i++){ // jesli jest jakies pole puste to nie ma jeszcze remisu
		if(shared_data->pola[i] == 0)
			return 0;
	}

	return 2; // remis
}

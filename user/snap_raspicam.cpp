#include <unistd.h>
#include <ctime>
#include <fstream>
#include <iostream>
#include <raspicam/raspicam.h>

#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <signal.h>
#include <sys/time.h>

using namespace std;

#define BUFFSIZE 256
#define SIG_TEST 44 /* we define our own signal, hard coded since SIGRTMIN is different in user and in kernel space */ 

raspicam::RaspiCam Camera; //Cmaera object

int takePictureNow ( void ) 
{
	Camera.grab();
	//allocate memory
	unsigned char *data=new unsigned char[  Camera.getImageTypeSize ( raspicam::RASPICAM_FORMAT_RGB )];
	//extract the image in rgb format
	Camera.retrieve ( data,raspicam::RASPICAM_FORMAT_RGB );//get camera image
	//save
	std::ofstream outFile ( "raspicam_image.ppm",std::ios::binary );
	outFile<<"P6\n"<<Camera.getWidth() <<" "<<Camera.getHeight() <<" 255\n";
	outFile.write ( ( char* ) data, Camera.getImageTypeSize ( raspicam::RASPICAM_FORMAT_RGB ) );
	//free resrources    
	delete data;
	return 0;
}

void receiveData(int n, siginfo_t *info, void *unused)
{
	struct timeval te; 
	gettimeofday(&te, NULL);
	takePictureNow ( );
	printf("Picture taken at %d\n", te.tv_usec);
}

int openCamera ( void)
{
	//Open camera 
	if ( !Camera.open()) {cerr<<"Error opening camera"<<endl;return -1;}
	return 0;
}

int main(int argc, char *argv[])
{
	int iPid = 1;	
	int iDelay = 60000;	
	int receivelen;
	int received = 0;
	char buffer[BUFFSIZE];
	struct sockaddr_in receivesocket;
	int sock;
	char byteBuffer[10];
	int iFileSource;
	//
	printf("Started \n");
	openCamera ( );
	printf("Camera opened \n");
	sleep(3);
	printf("Waiting for signal \n");
	// Read pid value from file	
	iFileSource = open("/sys/module/spi_gps/parameters/udp_pid", O_RDONLY);
	read(iFileSource, byteBuffer, 2);
	iPid = atoi(byteBuffer);
	close(iFileSource);
	if (iPid != 1)
	{
		printf("Fail to access GPS module. \n");
		sleep (iDelay / 1000);
		return 0;
	}
	//printf("Ipid from file %d \n", iPid);
	// Write pid value to file	
	sprintf (byteBuffer, "%d", getpid());
	printf("Ipid from system %d \n", getpid());
	//
	for (;;)
	{
		iFileSource = open("/sys/module/spi_gps/parameters/udp_pid", O_WRONLY | O_TRUNC | O_CREAT);
		write(iFileSource, byteBuffer, strlen(byteBuffer));
		close(iFileSource);
		//
		struct sigaction sig;
		sig.sa_sigaction = receiveData;
		sig.sa_flags = SA_SIGINFO;
		sigaction(SIG_TEST, &sig, NULL);
		//
		sleep (iDelay / 1000 + 2);
		//printf("Done \n");
	}
	//
	return 0;
}


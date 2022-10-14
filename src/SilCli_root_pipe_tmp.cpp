/*******************************************************************************
*                                                                              *
*                         Simone Valdre' - 14/10/2022                          *
*                  distributed under GPL-3.0-or-later licence                  *
*                                                                              *
*******************************************************************************/

// Parent process -> GUI, data fetcher and output writer
// Child  process -> Buffer reading clock (it simply generates a periodic signal)

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <ctime>

#include <unistd.h>
#include <signal.h>
#include <zmq.h>

#include "../include/SilCli_root.h"
#include "../include/SilStruct.h"

#define WINDOWX 1500
#define WINDOWY 800

#define CANVASX 964
#define CANVASY 400

#define MINICANVASX 320
#define MINICANVASY 320

#define FETCHINT 100000

static MyMainFrame *me;
pid_t cproc;
bool go, waitpipe;

uint64_t ts;
uint32_t dt;
uint16_t val, emask;

//Parent signal handler
extern "C" {
	static void handlesig_parent(int sig) {
		if(sig == SIGINT) {
			printf("\n[parent] SIGINT catched and IGNORED => please use Quit button to exit!!!\n");
			return;
		}
		else if(sig == SIGUSR1) {
			waitpipe = false;
			return;
		}
// 		me->refresh();
		return;
	}
}

//Child signal handler
void handlesig_child(int sig) {
	if(sig == SIGINT) {
		printf("\n[child ] SIGINT catched and IGNORED => please use Quit button to exit!!!\n");
		return;
	}
	else if(sig == SIGUSR1) {
		waitpipe = false;
		return;
	}
	else go = false;
	return;
}

void MyMainFrame::Connect() {
	bool fail = true;
	char buffer[1000];
	int N;
	sprintf(buffer, "tcp://%s:4747", tehost->GetText());
	if(write(pout, buffer, strlen(buffer) + 1) < 0) {
		perror("[parent] write");
		lout->SetText("Software bug on pipes (write)!!");
		return;
	}
	waitpipe = true;
	kill(cproc, SIGUSR1);
	tehost->SetEnabled(kFALSE);
	cbbits->SetEnabled(kFALSE);
	tepre->SetEnabled(kFALSE);
	tbconn->SetEnabled(kFALSE);
	
	sleep(5);
	if(waitpipe) {
		lout->SetText("Connection timeout!");
	}
	else {
		N = read(pin, buffer, 1000);
		if(N < 0) {
			perror("[parent] read");
			lout->SetText("Software bug on pipes (read)!!");
		}
		else if(N > 0) {
			if(strcmp(buffer, "OK")) {
				lout->SetText("Connection failed!");
			}
			else {
				lout->SetText("Connection with server established");
				fail = false;
			}
		}
		else lout->SetText("Software bug on pipes (no answer)!!");
	}
	if(fail) {
		tehost->SetEnabled(kTRUE);
		cbbits->SetEnabled(kTRUE);
		tepre->SetEnabled(kTRUE);
		tbconn->SetEnabled(kTRUE);
		return;
	}
	tbstart->SetEnabled(kTRUE);
	tbmanu->SetEnabled(kTRUE);
	tbauto->SetEnabled(kTRUE);
	
	//I create hbkg BEFORE creating the output file because I don't want it in the file!
	hbkg = new TH2F("hbkg", "Event rate", 1440, 0, 86400, 1000, 0, 10000);
	
	if(fout && (fout->IsZombie() == kFALSE)) {
		fout->Write();
		fout->Purge();
		fout->Close();
	}
	if(fout) delete fout;
	
	range = 0;
	do sprintf(buffer, "%s%05d.root", tepre->GetText(), range++);
	while(access(buffer, F_OK) == 0);
	
	fout = new TFile(buffer, "RECREATE");
	if(fout->IsZombie()) {
		lout->SetText("Bad output file. DISK STORAGE DISABLED!");
		delete fout;
		fout = nullptr;
	}
	else {
		lout->SetText(Form("Writing on %s", buffer));
		SetupTree();
	}
	SetupHistos();
	
	int bits = 10 + cbbits->GetSelected();
	range = (1 << bits);
	
	istat = 1;
	testat->SetText(stat[istat]);
	return;
}

void MyMainFrame::SetupTree() {
	fout->cd();
	tree = new TTree("silena", "Silena ADC data");
	
	tree->Branch("ts", &ts, "ts/l");
	tree->Branch("dt", &dt, "dt/i");
	tree->Branch("val", &val, "val/s");
	tree->Branch("emask", &emask, "emask/s");
	return;
}

void MyMainFrame::SetupHistos() {
	if(fout) fout->cd();
	
	hspe = new TH1D("hspe", "Energy spectrum", range, 0, range);
	hdead = new TH1D("hdead", "Dead time distribution", 1000, 0, 200);
	hrate = new TH1D("hrate", "Event interval distribution", 10000, 0, 100);
	
	gall = new TGraph();
	gall->SetName("gall");
	fout->Add(gall);
	
	glive = new TGraph();
	glive->SetName("glive");
	fout->Add(glive);
	return;
}

void MyMainFrame::Start() {
	if(write(pout, "start", 6) < 0) {
		perror("[parent] write");
		lout->SetText("Software bug on pipes (write)!!");
		return;
	}
	kill(cproc, SIGUSR1);
	
	tbstart->SetEnabled(kFALSE);
	tbstop->SetEnabled(kTRUE);
	pbrate->SetBarColor("green");
	pbdead->SetBarColor("red");
	pbbuff->SetBarColor("orange");
}

void MyMainFrame::Stop() {
	tbstart->SetEnabled(kTRUE);
	tbstop->SetEnabled(kFALSE);
	pbrate->SetPosition(4);
	pbrate->SetBarColor("gray");
	pbdead->SetPosition(4);
	pbdead->SetBarColor("gray");
	pbbuff->SetPosition(4);
	pbbuff->SetBarColor("gray");
}

void MyMainFrame::Terminate() {
	kill(cproc, SIGUSR2);
	usleep(100000);
	//Sometimes zmq_ctx_destroy hangs => I send another SIGUSR2!
	kill(cproc, SIGUSR2);
	usleep(10000);
	
	hbkg->Delete();
	
	if(fout && (fout->IsZombie() == kFALSE)) {
		fout->Write();
		fout->Purge();
		fout->Close();
	}
	if(fout) delete fout;
	
	fMain->Cleanup();
	delete fMain;
	close(pin);
	close(pout);
	printf("[parent] End of process\n");
	gApplication->Terminate(0);
	return;
}


// void MyMainFrame::refresh() {
// 	double trig[12],time,dead;
// 	int bitmask;
// 	
// 	read(pd[0],(void *)(&time),sizeof(double));
// 	read(pd[0],(void *)trig,12*sizeof(double));
// 	read(pd[0],(void *)(&bitmask),sizeof(int));
// 	dead=100.*(1.-trig[11]/trig[0]);
// 	
// 	val->SetText(Form("%.0lf",trig[11]/time));
// 	pbtrg->Reset();
// 	if(trig[0]/time<1) pbtrg->SetPosition(0);
// 	else pbtrg->SetPosition(log10(trig[0]/time));
// 	trg->SetText(Form("%.0lf",trig[0]/time));
// 	for(int i=0;i<10;i++) {
// 		pbt[i]->Reset();
// 		if(trig[1+i]/time<1) pbt[i]->SetPosition(0);
// 		else pbt[i]->SetPosition(log10(trig[1+i]/time));
// 		
// 		tet[i]->SetText(Form("%.0lf",trig[1+i]/time));
// 		if(i>7) continue;
// 		if(bitmask&(1<<i)) {
// 			tet[i]->SetAlignment(kTextRight);
// 			pbt[i]->SetBarColor("green");
// 		}
// 		else {
// 			tet[i]->SetAlignment(kTextLeft);
// 			tet[i]->SetText("disabled");
// 			pbt[i]->SetPosition(1000);
// 			pbt[i]->SetBarColor("gray");
// 		}
// 	}
// 	dt->SetText(Form("%.1lf %%",dead));
// 	return;
// }

MyMainFrame::MyMainFrame(const TGWindow *p, UInt_t w, UInt_t h, int pd1, int pd2) {
	me = this;
	pin = pd1; pout = pd2;
	istat = 0;
	fout = nullptr;
	signal(SIGINT, handlesig_parent);
	signal(SIGUSR1, handlesig_parent);
	
	FontStruct_t font_sml = gClient->GetFontByName("-*-arial-regular-r-*-*-16-*-*-*-*-*-iso8859-1");
	FontStruct_t font_big = gClient->GetFontByName("-*-arial-regular-r-*-*-24-*-*-*-*-*-iso8859-1");
	FontStruct_t font_lrg = gClient->GetFontByName("-*-arial-bold-r-*-*-32-*-*-*-*-*-iso8859-1");
	TGTextButton *tbexit;
	
	// Create a main frame
	fMain = new TGMainFrame(p, w, h);
	//hf00 starts
	TGHorizontalFrame *hf00=new TGHorizontalFrame(fMain);
	{
		//vf10 starts
		TGVerticalFrame *vf10=new TGVerticalFrame(hf00);
		{
			//***** Spectra and monitoring go here!
			fEcanvas = new TRootEmbeddedCanvas("Ecanvas0", vf10, CANVASX, CANVASY);
			vf10->AddFrame(fEcanvas, new TGLayoutHints(0, 2, 2, 2, 2));
// 			fEcanvas->GetCanvas()->Connect("ProcessedEvent(Int_t,Int_t,Int_t,TObject*)", "MyMainFrame", this, "HandleMyCanvas(Int_t,Int_t,Int_t,TObject*)");
			
			//hf11 starts
			TGHorizontalFrame *hf11=new TGHorizontalFrame(vf10);
			for(int i = 0; i < 3; i++) {
				fMini[i] = new TRootEmbeddedCanvas(Form("Ecanvas%d", i + 1), hf11, MINICANVASX, MINICANVASY);
				hf11->AddFrame(fMini[i], new TGLayoutHints(0, 1, 1, 1, 1));
			}
			//hf11 ends
			vf10->AddFrame(hf11, new TGLayoutHints(kLHintsExpandX|kLHintsCenterX|kLHintsCenterY, 1, 1, 1, 1));
		}
		//vf10 ends
		hf00->AddFrame(vf10, new TGLayoutHints(kLHintsExpandY|kLHintsCenterX|kLHintsCenterY, 2, 1, 2, 2));
		//vf20 starts
		TGVerticalFrame *vf20=new TGVerticalFrame(hf00);
		{
			//***** DAQ controls go here!
			TGLabel *ltit = new TGLabel(vf20, "SilPi acquisition client");
			ltit->SetTextFont(font_lrg);
			vf20->AddFrame(ltit, new TGLayoutHints(kLHintsCenterX, 2, 2, 20, 20));
			
			//hf21 starts
			TGHorizontalFrame *hf21=new TGHorizontalFrame(vf20);
			{
				TGLabel *lhost = new TGLabel(hf21, "RPi host");
				lhost->SetTextFont(font_sml);
				hf21->AddFrame(lhost, new TGLayoutHints(kLHintsCenterY|kLHintsExpandX, 2, 1, 2, 2));
				
				tehost = new TGTextEntry(hf21, "10.253.2.47");
				tehost->SetFont(font_sml);
				tehost->Resize(300, 24);
				tehost->SetAlignment(kTextRight);
				hf21->AddFrame(tehost, new TGLayoutHints(kLHintsCenterY, 1, 2, 2, 2));
			}
			//hf21 ends
			vf20->AddFrame(hf21, new TGLayoutHints(kLHintsExpandX|kLHintsCenterX|kLHintsCenterY, 2, 2, 2, 2));
			
			//hf22 starts
			TGHorizontalFrame *hf22=new TGHorizontalFrame(vf20);
			{
				TGLabel *lbits = new TGLabel(hf22, "Silena bits");
				lbits->SetTextFont(font_sml);
				hf22->AddFrame(lbits, new TGLayoutHints(kLHintsCenterY|kLHintsExpandX, 2, 1, 2, 2));
				
				cbbits = new TGComboBox(hf22);
				for(int i = 0; i < 7; i++) cbbits->AddEntry(Form("%d", 10 + i), i);
				cbbits->Select(3);
				cbbits->Resize(300, 24);
// 				cbbits->Connect("Selected(Int_t)", "MyMainFrame", this, "GasSwitch(Int_t)");
				hf22->AddFrame(cbbits, new TGLayoutHints(kLHintsCenterY, 1, 2, 2, 2));
				
			}
			//hf22 ends
			vf20->AddFrame(hf22, new TGLayoutHints(kLHintsExpandX|kLHintsCenterX|kLHintsCenterY, 2, 2, 2, 2));
			
			//hf23 starts
			TGHorizontalFrame *hf23=new TGHorizontalFrame(vf20);
			{
				TGLabel *lpre = new TGLabel(hf23, "Output prefix");
				lpre->SetTextFont(font_sml);
				hf23->AddFrame(lpre, new TGLayoutHints(kLHintsCenterY|kLHintsExpandX, 2, 1, 2, 2));
				
				tepre = new TGTextEntry(hf23, "out/acq");
				tepre->SetFont(font_sml);
				tepre->Resize(300, 24);
				tepre->SetAlignment(kTextRight);
				hf23->AddFrame(tepre, new TGLayoutHints(kLHintsCenterY, 1, 2, 2, 2));
			}
			//hf23 ends
			vf20->AddFrame(hf23, new TGLayoutHints(kLHintsExpandX|kLHintsCenterX|kLHintsCenterY, 2, 2, 2, 2));
			
			tbconn = new TGTextButton(vf20, "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\nAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\nAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA");
			tbconn->SetFont(font_big);
			tbconn->Connect("Clicked()", "MyMainFrame", this, "Connect()");
			vf20->AddFrame(tbconn, new TGLayoutHints(kLHintsCenterY, 2, 2, 10, 2));
			
			lout = new TGLabel(vf20, "Ready to connect!");
			lout->SetTextFont(font_sml);
			vf20->AddFrame(lout, new TGLayoutHints(kLHintsCenterY|kLHintsExpandX, 2, 2, 20, 20));
			
			//hf24 starts
			TGHorizontalFrame *hf24=new TGHorizontalFrame(vf20);
			{
				tbstart = new TGTextButton(hf24, "AAAAAAAAAAAAAAAAAAAAAAAAAAAl\nAAAAAAAAAAAAAAAAAAAAAAAAAAAl\nAAAAAAAAAAAAAAAAAAAAAAAAAAAl");
				tbstart->SetFont(font_big);
				tbstart->SetEnabled(kFALSE);
// 				tbstart->Connect("Clicked()", "MyMainFrame", this, "Calculate()");
				hf24->AddFrame(tbstart, new TGLayoutHints(kLHintsCenterY, 2, 1, 2, 2));
				
				tbstop = new TGTextButton(hf24, "AAAAAAAAAAAAAAAAAAAAAAAAAAAl\nAAAAAAAAAAAAAAAAAAAAAAAAAAAl\nAAAAAAAAAAAAAAAAAAAAAAAAAAAl");
				tbstop->SetFont(font_big);
				tbstop->SetEnabled(kFALSE);
// 				tbstop->Connect("Clicked()", "MyMainFrame", this, "Calculate()");
				hf24->AddFrame(tbstop, new TGLayoutHints(kLHintsCenterY, 1, 2, 2, 2));
			}
			//hf24 ends
			vf20->AddFrame(hf24, new TGLayoutHints(kLHintsExpandX|kLHintsCenterX|kLHintsCenterY, 2, 2, 2, 2));
			
			//hf25 starts
			TGHorizontalFrame *hf25=new TGHorizontalFrame(vf20);
			{
				TGLabel *lstat = new TGLabel(hf25, "DAQ status");
				lstat->SetTextFont(font_big);
				hf25->AddFrame(lstat, new TGLayoutHints(kLHintsCenterY|kLHintsExpandX, 2, 1, 2, 2));
				
				testat = new TGTextEntry(hf25, stat[istat]);
				testat->SetFont(font_big);
				testat->Resize(300, 32);
				testat->SetEnabled(kFALSE);
				testat->SetAlignment(kTextCenterX);
				hf25->AddFrame(testat, new TGLayoutHints(kLHintsCenterY, 1, 2, 2, 2));
			}
			//hf25 ends
			vf20->AddFrame(hf25, new TGLayoutHints(kLHintsExpandX|kLHintsCenterX|kLHintsCenterY, 2, 2, 2, 20));
			
			//hf26 starts
			TGHorizontalFrame *hf26=new TGHorizontalFrame(vf20);
			{
				TGLabel *lrate = new TGLabel(hf26, "Acq. rate");
				lrate->SetTextFont(font_sml);
				hf26->AddFrame(lrate, new TGLayoutHints(kLHintsCenterY|kLHintsExpandX, 2, 1, 2, 2));
				
				pbrate = new TGHProgressBar(hf26, 300, 24);
				pbrate->SetRange(0, 4);
				pbrate->SetPosition(4);
				pbrate->SetBarColor("gray");
				pbrate->ShowPosition(kFALSE);
				hf26->AddFrame(pbrate, new TGLayoutHints(kLHintsCenterY, 1, 2, 2, 2));
			}
			//hf26 ends
			vf20->AddFrame(hf26, new TGLayoutHints(kLHintsExpandX|kLHintsCenterX|kLHintsCenterY, 2, 2, 2, 2));
			
			//hf27 starts
			TGHorizontalFrame *hf27=new TGHorizontalFrame(vf20);
			{
				TGLabel *ldead = new TGLabel(hf27, "Dead time");
				ldead->SetTextFont(font_sml);
				hf27->AddFrame(ldead, new TGLayoutHints(kLHintsCenterY|kLHintsExpandX, 2, 1, 2, 2));
				
				pbdead = new TGHProgressBar(hf27, 300, 24);
				pbdead->SetRange(0, 4);
				pbdead->SetPosition(4);
				pbdead->SetBarColor("gray");
				pbdead->ShowPosition(kFALSE);
				hf27->AddFrame(pbdead, new TGLayoutHints(kLHintsCenterY, 1, 2, 2, 2));
			}
			//hf27 ends
			vf20->AddFrame(hf27, new TGLayoutHints(kLHintsExpandX|kLHintsCenterX|kLHintsCenterY, 2, 2, 2, 2));
			
			//hf28 starts
			TGHorizontalFrame *hf28=new TGHorizontalFrame(vf20);
			{
				TGLabel *lbuff = new TGLabel(hf28, "Buffer");
				lbuff->SetTextFont(font_sml);
				hf28->AddFrame(lbuff, new TGLayoutHints(kLHintsCenterY|kLHintsExpandX, 2, 1, 2, 2));
				
				pbbuff = new TGHProgressBar(hf28, 300, 24);
				pbbuff->SetRange(0, 4);
				pbbuff->SetPosition(4);
				pbbuff->SetBarColor("gray");
				pbbuff->ShowPosition(kFALSE);
				hf28->AddFrame(pbbuff, new TGLayoutHints(kLHintsCenterY, 1, 2, 2, 2));
			}
			//hf28 ends
			vf20->AddFrame(hf28, new TGLayoutHints(kLHintsExpandX|kLHintsCenterX|kLHintsCenterY, 2, 2, 2, 2));
			
			//hf29 starts
			TGHorizontalFrame *hf29=new TGHorizontalFrame(vf20);
			{
				tbmanu = new TGTextButton(hf29, "AAAAAAAAAAAAAAAAAAAA\nAAAAAAAAAAAAAAAAAAAA");
				tbmanu->SetFont(font_sml);
				tbmanu->SetEnabled(kFALSE);
// 				tbmanu->Connect("Clicked()", "MyMainFrame", this, "Calculate()");
				hf29->AddFrame(tbmanu, new TGLayoutHints(kLHintsCenterY, 2, 2, 2, 2));
				
				tbauto = new TGCheckButton(hf29, "AAAAAAAAAAAAAAAAAA\nAAAAAAAAAAAAAAAAAA");
				tbauto->SetFont(font_sml);
				tbauto->SetEnabled(kFALSE);
// 				tbauto->Connect("Clicked()", "MyMainFrame", this, "Calculate()");
				hf29->AddFrame(tbauto, new TGLayoutHints(kLHintsCenterY, 2, 2, 2, 2));
			}
			//hf29 ends
			vf20->AddFrame(hf29, new TGLayoutHints(kLHintsExpandX|kLHintsCenterX|kLHintsCenterY, 2, 2, 20, 2));
			
			tbexit = new TGTextButton(vf20, "AAAAAAAAAAAAAAAAAAAAAAAAAAAA\nAAAAAAAAAAAAAAAAAAAAAAAAAAAA");
			tbexit->Connect("Clicked()", "MyMainFrame", this, "Terminate()");
			tbexit->SetFont(font_big);
			vf20->AddFrame(tbexit, new TGLayoutHints(kLHintsCenterY, 2, 2, 2, 2));
		}
		//vf20 ends
		hf00->AddFrame(vf20, new TGLayoutHints(kLHintsExpandY|kLHintsCenterX|kLHintsCenterY, 1, 2, 2, 2));
	}
	//hf0 ends
	fMain->AddFrame(hf00, new TGLayoutHints(kLHintsCenterX|kLHintsCenterY|kLHintsExpandY|kLHintsExpandX, 1, 1, 1, 1));
	
	// Set a name to the main frame
	fMain->SetWindowName("SilPi DAQ client");
	
	// Map all subwindows of main frame
	fMain->MapSubwindows();
	
	// Initialize the layout algorithm
	fMain->Resize(fMain->GetDefaultSize());
	
	// Map main frame
	fMain->MapWindow();
	
	tbconn->SetText("\nCONNECT                 ");
	tbstart->SetText("\nSTART            ");
	tbstop->SetText("\nSTOP          ");
	tbmanu->SetText("Manual update");
	tbauto->SetText("AUTO update");
	tbexit->SetText("&Quit DAQ");
}

// void MyMainFrame::close_cycle() {
// 	kill(cproc,SIGUSR1);
// 	sleep(1);
// 	gApplication->Terminate(0);
// 	return;
// }

MyMainFrame::~MyMainFrame() {
	// Clean up used widgets: frames, buttons, layout hints
	//THIS DESTRUCTOR IS NEVER CALLED!!! USE Terminate() function as destructor!
	
	fMain->Cleanup();
	delete fMain;
	printf("[parent] End of process xxx\n");
	return;
}

int main(int argc, char **argv) {
	int pd1[2], pd2[2];
	pipe(pd1);
	pipe(pd2);
	cproc = fork();
	
	if(cproc<0) {
		perror("fork");
	}
	else {
		if(cproc==0) { //processo figlio
			pid_t pproc = getppid();
			signal(SIGINT, handlesig_child);
			close(pd1[0]);
			close(pd2[1]);
			int pin  = pd2[0];
			int pout = pd1[1];
			char host[1000], buffer[1000];
			go = true;
			signal(SIGUSR2, handlesig_child);
			signal(SIGUSR1, handlesig_child);
			void *context   = zmq_ctx_new();
			void *requester = zmq_socket(context, ZMQ_REQ);
			int N = 3000; //setting a 3s timeout!
			if(zmq_setsockopt(requester, ZMQ_RCVTIMEO, &N, sizeof(N))) {
				perror("[child ] zmq_setsockopt");
			}
			for(;go;) {
				waitpipe = true;
				for(;go && waitpipe;) {
					pause();
				}
				if(!go) break;
				
				N = read(pin, host, 1000);
				if(N < 0) {
					perror("[child ] read");
					write(pout, "NC", 3);
					kill(pproc, SIGUSR1);
					continue;
				}
				
				printf("[child ] connecting to %s\n", host);
				if(zmq_connect(requester, host)) {
					perror("[child ] zmq_connect");
					write(pout, "NC", 3);
					kill(pproc, SIGUSR1);
					continue;
				}
				
				printf("[child ] checking server status\n");
				if(zmq_send(requester, "check", 6, 0) < 0) {
					perror("[child ] zmq_send");
					write(pout, "NC", 3);
					kill(pproc, SIGUSR1);
					zmq_disconnect(requester, host);
					continue;
				}
				
				N = zmq_recv(requester, buffer, 999, 0);
				if(N < 0) {
					perror("[child ] zmq_recv");
					write(pout, "NC", 3);
					kill(pproc, SIGUSR1);
					zmq_disconnect(requester, host);
					continue;
				}
				buffer[N] = '\0';
				
				printf("[child ] answer received -> %s\n", buffer);
				if(strcmp(buffer, "ACK") == 0) {
					write(pout, "OK", 3);
					kill(pproc, SIGUSR1);
					break;
				}
				
				printf("[child] server check failed\n");
				zmq_disconnect(requester, host);
				write(pout, "NC", 3);
				kill(pproc, SIGUSR1);
			}
			
			
			
			
			zmq_close(requester);
			zmq_ctx_destroy(context);
			close(pd1[1]);
			close(pd2[0]);
			printf("[child ] End of process\n");
			return 0;
		}
		else { //processo genitore
			signal(SIGINT, handlesig_parent);
			close(pd1[1]);
			close(pd2[0]);
			TApplication theApp("App", &argc, argv);
			new MyMainFrame(gClient->GetRoot(), WINDOWX, WINDOWY, pd1[0], pd2[1]);
			theApp.Run();
		}
	}
	return 0;
}

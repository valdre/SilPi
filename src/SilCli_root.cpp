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

#include <unistd.h>
#include <signal.h>
#include <sys/time.h>
#include <zmq.h>

#include "../include/SilCli_root.h"
#include "../include/SilStruct.h"

#define WINDOWX 1500
#define WINDOWY 800

#define CANVASX 964
#define CANVASY 400

#define MINICANVASX 320
#define MINICANVASY 320

//Data fetch interval (in us)
#define FETCHINT 100000
//Histograms and Graphs update interval (in ms)
#define HISTUP     5000L
#define GRAPHUP   60000L

static MyMainFrame *me;
pid_t cproc;
bool go, waitpipe;

uint64_t ts;
uint32_t dt;
uint16_t val, emask;

//Parent signal handler
extern "C" {
	static void handlesig_parent(int sig) {
		if(sig == SIGINT) printf("\n[parent] SIGINT catched and IGNORED => please use Quit button to exit!!!\n");
		else if(sig == SIGUSR1) me->Fetch();
		return;
	}
}

//Child signal handler
void handlesig_child(int sig) {
	if(sig == SIGINT) printf("\n[child ] SIGINT catched and IGNORED => please use Quit button to exit!!!\n");
	else go = false;
	return;
}

void MyMainFrame::Connect() {
	char buffer[1000];
	int N = 100; //setting a 100ms timeout!
	
	if(context) printf("[parent] ZMQ context already exists!\n");
	else context = zmq_ctx_new();
	if(requester) printf("[parent] ZMQ socket already exists!\n");
	else requester = zmq_socket(context, ZMQ_REQ);
	if(zmq_setsockopt(requester, ZMQ_RCVTIMEO, &N, sizeof(N))) {
		perror("[parent] zmq_setsockopt");
	}
	
	lout->SetText("Connection failed!");
	
	sprintf(buffer, "tcp://%s:4747", tehost->GetText());
	printf("[parent] connecting to %s\n", buffer);
	if(zmq_connect(requester, buffer)) {
		perror("[parent] zmq_connect");
		zmq_close(requester);
		requester = nullptr;
		zmq_ctx_destroy(context);
		context = nullptr;
		return;
	}
	
	printf("[parent] checking server status\n");
	if(zmq_send(requester, "check", 6, 0) < 0) {
		perror("[parent] zmq_send");
		zmq_close(requester);
		requester = nullptr;
		zmq_ctx_destroy(context);
		context = nullptr;
		return;
	}
	
	N = zmq_recv(requester, buffer, 999, 0);
	if(N < 0) {
		perror("[parent] zmq_recv");
		zmq_close(requester);
		requester = nullptr;
		zmq_ctx_destroy(context);
		context = nullptr;
		return;
	}
	buffer[N] = '\0';
	
	printf("[parent] answer received -> %s\n", buffer);
	if(strcmp(buffer, "ACK")) {
		printf("[child] server check failed\n");
		zmq_close(requester);
		requester = nullptr;
		zmq_ctx_destroy(context);
		context = nullptr;
		return;
	}
	
	tehost->SetEnabled(kFALSE);
	cbbits->SetEnabled(kFALSE);
	tepre->SetEnabled(kFALSE);
	tbconn->SetEnabled(kFALSE);
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
	
	int bits = 10 + cbbits->GetSelected();
	range = (1 << bits);
	
	SetupHistos();
	
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
	
	//Canvas setup!
	TCanvas *fCanvas = fEcanvas->GetCanvas();
	fCanvas->GetPad(0)->SetGridx(kFALSE);
	fCanvas->GetPad(0)->SetGridy(kFALSE);
	fCanvas->GetPad(0)->SetLogy(kTRUE);
	fCanvas->cd();
	hspe->GetXaxis()->SetRangeUser(2, range);
	hspe->SetStats(kFALSE);
	hspe->Draw();
	fCanvas->Modified();
	fCanvas->Update();
	
	fCanvas = fMini[0]->GetCanvas();
	fCanvas->GetPad(0)->SetGridx(kFALSE);
	fCanvas->GetPad(0)->SetGridy(kFALSE);
	fCanvas->GetPad(0)->SetLogy(kTRUE);
	fCanvas->cd();
	hbkg->GetXaxis()->SetTimeDisplay(kTRUE);
	hbkg->GetXaxis()->SetNdivisions(503);
	hbkg->GetXaxis()->SetTimeFormat("%H:%M");
	hbkg->GetXaxis()->SetTimeOffset(0, "gmt");
	hbkg->SetStats(kFALSE);
	hbkg->Draw();
	fCanvas->Modified();
	fCanvas->Update();
	gall->SetLineColor(kBlue + 2);
	glive->SetLineColor(kGreen + 2);
	
	fCanvas = fMini[1]->GetCanvas();
	fCanvas->GetPad(0)->SetGridx(kFALSE);
	fCanvas->GetPad(0)->SetGridy(kFALSE);
	fCanvas->GetPad(0)->SetLogy(kTRUE);
	fCanvas->cd();
	hdead->SetStats(kFALSE);
	hdead->Draw();
	fCanvas->Modified();
	fCanvas->Update();
	
	fCanvas = fMini[2]->GetCanvas();
	fCanvas->GetPad(0)->SetGridx(kFALSE);
	fCanvas->GetPad(0)->SetGridy(kFALSE);
	fCanvas->cd();
	hrate->SetStats(kFALSE);
	hrate->Draw();
	fCanvas->Modified();
	fCanvas->Update();
	return;
}

void MyMainFrame::Start() {
	int N;
	char buffer[1000];
	
	if(zmq_send(requester, "start", 6, 0) < 0) {
		perror("[parent] zmq_send");
		lout->SetText("Server connection failed");
		return;
	}
	
	N = zmq_recv(requester, buffer, 999, 0);
	if(N < 0) {
		perror("[parent] zmq_recv");
		lout->SetText("Server connection failed");
		return;
	}
	buffer[N] = '\0';
	printf("[parent] START -> %s\n", buffer);
	gettimeofday(&ti, NULL);
	count = 1;
	t0 = 0; lastts = 0; tall = 0; tdead = 0; lasttah = 0; lasttag = 0; lasttdh = 0; lasttdg = 0; lastNh = 0; lastNg = 0;
	Nev = 0; lasthup = 0; lastgup = 0; buffil = 0; Nbuf = 0;
	
	istat = 2;
	testat->SetText(stat[istat]);
	
	tbstart->SetEnabled(kFALSE);
	tbstop->SetEnabled(kTRUE);
	pbrate->SetBarColor("green");
	pbdead->SetBarColor("red");
	pbbuff->SetBarColor("orange");
	return;
}

void MyMainFrame::Stop() {
	int N;
	char buffer[1000];
	
	if(zmq_send(requester, "stop", 5, 0) < 0) {
		perror("[parent] zmq_send");
		lout->SetText("Server connection failed");
		return;
	}
	
	N = zmq_recv(requester, buffer, 999, 0);
	if(N < 0) {
		perror("[parent] zmq_recv");
		lout->SetText("Server connection failed");
		return;
	}
	buffer[N] = '\0';
	printf("[parent]  STOP -> %s\n", buffer);
	
	istat = 1;
	testat->SetText(stat[istat]);
	
	tbstart->SetEnabled(kTRUE);
	tbstop->SetEnabled(kFALSE);
	pbrate->SetPosition(4);
	pbrate->SetBarColor("gray");
	pbdead->SetPosition(1);
	pbdead->SetBarColor("gray");
	pbbuff->SetPosition(1);
	pbbuff->SetBarColor("gray");
	return;
}

void MyMainFrame::Fetch() {
	if(istat != 2) return;
	
	int N;
	struct Silevent data[SIZE];
	struct timeval tf, td;
	
	zmq_send(requester, "send", 5, 0); //5 byte: c'è il carattere terminatore '\0'!!
	N = zmq_recv(requester, data, SIZE * sizeof(struct Silevent), 0);
	
	if(N % sizeof(struct Silevent)) {
		printf("[parent] read fraction of event (size = %d)\n", N);
	}
	N /= sizeof(struct Silevent);
	buffil += (double)N;
	Nbuf += 1;
	
	for(int j = 0; j < N; j++) {
		if(t0 == 0) {
			//always skip first "count" non-zero events (used as start time mark, first is usually not reliable)
			t0 = data[j].ts + (uint64_t)(data[j].dt);
			if(t0 && count) {
				count--;
				t0 = 0;
			}
			if(t0) {
				gettimeofday(&ti, NULL);
				lastts = t0;
			}
			continue;
		}
		tall = data[j].ts + (uint64_t)(data[j].dt) - t0;
		tdead += (uint64_t)(data[j].dt);
		hspe->Fill(data[j].val);
		hdead->Fill((double)(data[j].dt) / 1000.);
		hrate->Fill(((double)(data[j].ts - lastts)) / 1000000.);
		Nev++;
		lastts = data[j].ts;
		
		ts = data[j].ts; dt = data[j].dt;
		val = data[j].val; emask = data[j].emask;
		tree->Fill();
	}
	if(Nev == 0) return;
	
	gettimeofday(&tf, NULL);
	timersub(&tf, &ti, &td);
	uint64_t msec = ((uint64_t)td.tv_usec + 1000000L * (uint64_t)td.tv_sec + 500L) / 1000L;
	if(msec - lasthup >= HISTUP) {
		//SOLO SE AUTO UPDATE!!
		fEcanvas->GetCanvas()->Modified();
		fEcanvas->GetCanvas()->Update();
		fMini[1]->GetCanvas()->Modified();
		fMini[1]->GetCanvas()->Update();
		fMini[2]->GetCanvas()->Modified();
		fMini[2]->GetCanvas()->Update();
		
		double dead = (tall == lasttah) ? 0 : ((double)(tdead - lasttdh)) / ((double)(tall - lasttah));
		double buff = Nbuf ? buffil / (Nbuf * (double)SIZE) : 0;
		double rate = 1000. * ((double)(Nev - lastNh)) / ((double)(msec - lasthup));
		
		pbdead->Reset();
		pbdead->SetPosition(dead);
		pbbuff->Reset();
		pbbuff->SetPosition(buff);
		pbrate->Reset();
		if(rate) pbrate->SetPosition(log10(1 + rate));
		else pbrate->SetPosition(0);
		
		lastNh  = Nev;
		lasttah = tall;
		lasttdh = tdead;
		lasthup = msec;
		buffil  = 0; Nbuf = 0;
	}
	
	if(msec - lastgup >= GRAPHUP) {
		double dead = (tall == lasttag) ? 0 : ((double)(tdead - lasttdg)) / ((double)(tall - lasttag));
		
		double rate = 1000. * ((double)(Nev - lastNg)) / ((double)(msec - lastgup));
		double sec = (double)(tf.tv_sec % 86400L - GRAPHUP/2000);
		if(sec < 0.) sec += 86400.;
		double grange = 60. * ceil(sec / 60.);
		if(grange < 3600.) grange = 3600.;
		hbkg->GetXaxis()->SetRangeUser(grange - 3600., grange);
		
		fMini[0]->GetCanvas()->cd();
		gall->AddPoint(sec, rate / (1. - dead));
		glive->AddPoint(sec, rate);
		gall->Draw("L");
		glive->Draw("L same");
		fMini[0]->GetCanvas()->Modified();
		fMini[0]->GetCanvas()->Update();
		
		lastNg  = Nev;
		lastgup = msec;
		lasttag = tall;
		lasttdg = tdead;
	}
	
	return;
}

// void MyMainFrame::ManUpdate() {
// 
// 	return;
// }

MyMainFrame::MyMainFrame(const TGWindow *p, UInt_t w, UInt_t h) {
	me = this;
	istat = 0;
	context = nullptr;
	requester = nullptr;
	fout = nullptr;
	
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
				tbstart->Connect("Clicked()", "MyMainFrame", this, "Start()");
				hf24->AddFrame(tbstart, new TGLayoutHints(kLHintsCenterY, 2, 1, 2, 2));
				
				tbstop = new TGTextButton(hf24, "AAAAAAAAAAAAAAAAAAAAAAAAAAAl\nAAAAAAAAAAAAAAAAAAAAAAAAAAAl\nAAAAAAAAAAAAAAAAAAAAAAAAAAAl");
				tbstop->SetFont(font_big);
				tbstop->SetEnabled(kFALSE);
				tbstop->Connect("Clicked()", "MyMainFrame", this, "Stop()");
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
				pbdead->SetRange(0, 1);
				pbdead->SetPosition(1);
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
				pbbuff->SetRange(0, 1);
				pbbuff->SetPosition(1);
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
	
	return;
}

MyMainFrame::~MyMainFrame() {
	// Clean up used widgets: frames, buttons, layout hints
	//THIS DESTRUCTOR IS NEVER CALLED!!! USE Terminate() function as destructor!
	
	fMain->Cleanup();
	delete fMain;
	printf("[parent] End of process xxx\n");
	return;
}

void MyMainFrame::Terminate() {
	kill(cproc, SIGUSR2);
	usleep(10000);
	
	if(istat == 2) Stop();
	
	if(requester) zmq_close(requester);
	if(context) zmq_ctx_destroy(context);
	
	hbkg->Delete();
	
	if(fout && (fout->IsZombie() == kFALSE)) {
		fout->Write();
		fout->Purge();
		fout->Close();
	}
	if(fout) delete fout;
	
	fMain->Cleanup();
	delete fMain;
	printf("[parent] End of process\n");
	gApplication->Terminate(0);
	return;
}

int main(int argc, char **argv) {
	cproc = fork();
	
	if(cproc<0) {
		perror("fork");
	}
	else {
		if(cproc==0) { //processo figlio
			pid_t pproc = getppid();
			go = true;
			signal(SIGINT, handlesig_child);
			signal(SIGUSR2, handlesig_child);
			sleep(2);
			for(;go;) {
				usleep(FETCHINT);
				kill(pproc, SIGUSR1);
			}
			printf("[child ] End of process\n");
			return 0;
		}
		else { //processo genitore
			signal(SIGINT, handlesig_parent);
			signal(SIGUSR1, handlesig_parent);
			TApplication theApp("App", &argc, argv);
			new MyMainFrame(gClient->GetRoot(), WINDOWX, WINDOWY);
			theApp.Run();
		}
	}
	return 0;
}

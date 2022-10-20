/*******************************************************************************
*                                                                              *
*                         Simone Valdre' - 17/10/2022                          *
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
#include <errno.h>
#include <sys/time.h>
#include <zmq.h>

#include "../include/SilCli_root.h"
#include "../include/SilStruct.h"

#define WINDOWX 1500
#define WINDOWY 800

#define CANVASX 964
#define CANVASY 500

#define MINICANVASX 320
#define MINICANVASY 320

//Data fetch interval (in us)
#define FETCHINT 100000
//Histograms and Graphs update interval (in ms)
#define HISTUP     5000L

static MyMainFrame *me;
pid_t cproc;
bool go, fen;

uint64_t ts;
uint32_t dt;
uint16_t val, emask;

//Parent signal handler
extern "C" {
	static void handlesig_parent(int sig) {
		switch(sig) {
			case SIGINT: case SIGTERM:
				me->Terminate();
				break;
			case SIGUSR1:
				me->Fetch();
		}
		return;
	}
}

//Child signal handler
void handlesig_child(int sig) {
	switch(sig) {
		case SIGINT:
			//ignore SIGINT (handled by parent)
			break;
		case SIGUSR1:
			//enable data fetching
			fen = true;
			usleep(100000);
			break;
		case SIGUSR2:
			//disable data fetching
			fen = false;
			break;
		case SIGTERM:
			//exit process
			go = false;
	}
	return;
}

int MyMainFrame::Query(void *requester, const void *q, const size_t &qlen, void *ans, const size_t &alen, const int &type) {
	if(fTest) return 0;
	if(requester == nullptr) return -1;
	if(qtype) {
		printf("[parent] emptying buffer!\n");
		if(qtype == QTYPE_BUF) {
			char buffer[1000];
			int N = zmq_recv(requester, buffer, 999, 0);
			if(N < 0) {
				perror("[parent] zmq_recv");
				return -1;
			}
		}
		else if(qtype == QTYPE_DAT) {
			struct Silevent data[SIZE];
			int N = zmq_recv(requester, data, SIZE * sizeof(struct Silevent), 0);
			if(N < 0) {
				perror("[parent] zmq_recv");
				return -1;
			}
		}
		qtype = 0;
	}
	if(q == nullptr || ans == nullptr) return 0;
	
	qtype = type;
	if(zmq_send(requester, q, qlen, 0) < 0) {
		perror("[parent] zmq_send");
		return -1;
	}
	
	int N = zmq_recv(requester, ans, alen, 0);
	if(N < 0) {
		perror("[parent] zmq_recv");
		return -1;
	}
	qtype = 0;
	return N;
}

void MyMainFrame::Connect() {
	if(istat > 0) {
		PiDisconnect();
		return;
	}
	
	char buffer[1000];
	int N = 100; //setting a 100ms timeout!
	
	if(!fTest) {
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
		N = Query(requester, "check", 6, buffer, 999, QTYPE_BUF);
		if(N < 0) {
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
	}
	
	lout->SetText("Ready to start acquisition!");
	
	tehost->SetEnabled(kFALSE);
	tbconn->SetText("\nDISCONNECT                   ");
	cbbits->SetEnabled(kTRUE);
	tepre->SetEnabled(kTRUE);
	tbstart->SetEnabled(kTRUE);
// 	tbmanu->SetEnabled(kTRUE);
// 	tbauto->SetEnabled(kTRUE);
	
	istat = STAT_STOP;
	testat->SetText(stat[istat]);
	return;
}

void MyMainFrame::PiDisconnect() {
	if(istat > 1) Stop();
	if(requester && !fTest) {
		if(istat > 0) {
			char buffer[1000];
			int N = Query(requester, "exit", 5, buffer, 999, QTYPE_BUF);
			if(N >= 0) {
				buffer[N] = '\0';
				printf("[parent]  EXIT -> %s\n", buffer);
			}
		}
	}
	if(requester) {
		Query(requester, nullptr, 0, nullptr, 0, 0);
		zmq_close(requester);
	}
	if(context) zmq_ctx_destroy(context);
	
	tehost->SetEnabled(kTRUE);
	tbconn->SetText("\nCONNECT                 ");
	cbbits->SetEnabled(kFALSE);
	tepre->SetEnabled(kFALSE);
	tbstart->SetEnabled(kFALSE);
	
	lout->SetText("Ready to connect!");
	
	istat = STAT_NCFG;
	testat->SetText(stat[istat]);
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
	int range = (1 << (10 + cbbits->GetSelected()));
	
	if(fout) fout->cd();
	
	hspe = new TH1D("hspe", "", range, 0, range);
	hdead = new TH1D("hdead", "", 1000, 0, 200);
	hrate = new TH1D("hrate", "", 10000, 0, 100);
	
	gall = new TGraph();
	gall->SetName("gall");
	fout->Add(gall);
	
	glive = new TGraph();
	glive->SetName("glive");
	fout->Add(glive);
	
	//Canvas setup!
	TCanvas *fCanvas = fEcanvas->GetCanvas();
	fCanvas->cd();
	fCanvas->GetPad(0)->SetGridx(kFALSE);
	fCanvas->GetPad(0)->SetGridy(kFALSE);
	fCanvas->GetPad(0)->SetLogy(kTRUE);
	fCanvas->GetPad(0)->SetMargin(0.068, 0.01, 0.11, 0.02);
	
	hspe->SetLineColor(kBlack);
	hspe->SetLineWidth(2);
	hspe->GetXaxis()->SetRangeUser(2, range);
	hspe->GetXaxis()->SetTitle("Energy [ADC units]");
	hspe->GetXaxis()->SetTitleSize(0.05);
	hspe->GetXaxis()->SetLabelSize(0.05);
	hspe->GetYaxis()->SetTitle("Counts per ADC unit");
	hspe->GetYaxis()->SetTitleSize(0.05);
	hspe->GetYaxis()->SetTitleOffset(0.65);
	hspe->GetYaxis()->SetLabelSize(0.05);
	hspe->SetStats(kFALSE);
	hspe->Draw();
	fCanvas->Modified();
	fCanvas->Update();
	
	fCanvas = fMini[0]->GetCanvas();
	fCanvas->cd();
	fCanvas->GetPad(0)->SetGridx(kFALSE);
	fCanvas->GetPad(0)->SetGridy(kFALSE);
	fCanvas->GetPad(0)->SetLogy(kTRUE);
	fCanvas->GetPad(0)->SetMargin(0.17, 0.07, 0.12, 0.03);
	
	hbkg->GetXaxis()->SetTimeDisplay(kTRUE);
	hbkg->GetXaxis()->SetNdivisions(503);
	hbkg->GetXaxis()->SetTimeFormat("%H:%M");
	hbkg->GetXaxis()->SetTimeOffset(0, "gmt");
	hbkg->GetXaxis()->SetTitle("UTC time");
	hbkg->GetXaxis()->SetTitleSize(0.06);
	hbkg->GetXaxis()->SetTitleOffset(0.95);
	hbkg->GetXaxis()->SetLabelSize(0.06);
	hbkg->GetYaxis()->SetTitle("Event rate [ev/s]");
	hbkg->GetYaxis()->SetTitleSize(0.06);
	hbkg->GetYaxis()->SetTitleOffset(1.28);
	hbkg->GetYaxis()->SetLabelSize(0.06);
	hbkg->GetYaxis()->SetRangeUser(0.5, 10000);
	hbkg->SetStats(kFALSE);
	hbkg->Draw();
	fCanvas->Modified();
	fCanvas->Update();
	gall->SetLineColor(kBlue + 2);
	gall->SetLineWidth(2);
	glive->SetLineColor(kGreen + 2);
	glive->SetLineWidth(2);
	
	fCanvas = fMini[1]->GetCanvas();
	fCanvas->cd();
	fCanvas->GetPad(0)->SetGridx(kFALSE);
	fCanvas->GetPad(0)->SetGridy(kFALSE);
	fCanvas->GetPad(0)->SetLogy(kTRUE);
	fCanvas->GetPad(0)->SetMargin(0.17, 0.07, 0.12, 0.03);
	
	hdead->GetXaxis()->SetNdivisions(508);
	hdead->GetXaxis()->SetTitle("Dead time [#mus]");
	hdead->GetXaxis()->SetTitleSize(0.06);
	hdead->GetXaxis()->SetTitleOffset(0.95);
	hdead->GetXaxis()->SetLabelSize(0.06);
	hdead->GetYaxis()->SetTitle("Counts per bin");
	hdead->GetYaxis()->SetTitleSize(0.06);
	hdead->GetYaxis()->SetTitleOffset(1.28);
	hdead->GetYaxis()->SetLabelSize(0.06);
	hdead->SetStats(kFALSE);
	hdead->SetLineColor(kRed + 2);
	hdead->SetLineWidth(2);
	hdead->Draw();
	fCanvas->Modified();
	fCanvas->Update();
	
	fCanvas = fMini[2]->GetCanvas();
	fCanvas->cd();
	fCanvas->GetPad(0)->SetGridx(kFALSE);
	fCanvas->GetPad(0)->SetGridy(kFALSE);
	fCanvas->GetPad(0)->SetLogy(kTRUE);
	fCanvas->GetPad(0)->SetMargin(0.17, 0.07, 0.12, 0.03);
	
	hrate->GetXaxis()->SetNdivisions(508);
	hrate->GetXaxis()->SetTitle("Event interval [ms]");
	hrate->GetXaxis()->SetTitleSize(0.06);
	hrate->GetXaxis()->SetTitleOffset(0.95);
	hrate->GetXaxis()->SetLabelSize(0.06);
	hrate->GetYaxis()->SetTitle("Counts per bin");
	hrate->GetYaxis()->SetTitleSize(0.06);
	hrate->GetYaxis()->SetTitleOffset(1.28);
	hrate->GetYaxis()->SetLabelSize(0.06);
	hrate->SetStats(kFALSE);
	hrate->SetLineColor(kBlue + 2);
	hrate->SetLineWidth(2);
	hrate->Draw();
	fCanvas->Modified();
	fCanvas->Update();
	return;
}

void MyMainFrame::MultiButton() {
	switch(istat) {
		case STAT_STOP:
			Start();
			break;
		case STAT_STRT:
			Pause();
			break;
		case STAT_PAUS:
			Resume();
	}
	return;
}

void MyMainFrame::Start() {
	//If a file is already opened I close it!
	if(fout && (fout->IsZombie() == kFALSE)) {
		fout->Write();
		fout->Purge();
		fout->Close();
	}
	if(fout) delete fout;
	
	//I create hbkg BEFORE creating the output file because I don't want it in the file!
	hbkg = new TH2F("hbkg", "", 1440, 0, 86400, 1000, 0, 10000);
	
	char buffer[1000];
	do sprintf(buffer, "%s%05d.root", tepre->GetText(), fcnt++);
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
	
	istat = STAT_STRT;
	testat->SetText(stat[istat]);
	
	int N = Query(requester, "start", 6, buffer, 999, QTYPE_BUF);
	if(N < 0) {
		lout->SetText("Server connection failed");
		PiDisconnect();
		return;
	}
	else {
		buffer[N] = '\0';
		printf("[parent] START -> %s\n", buffer);
	}
	gettimeofday(&ti, NULL);
	t0 = 0; lastts = 0; tall = 0; tdead = 0; tpaused = 0; lasttall = 0; lasttdead = 0; lastN = 0;
	Nev = 0; Nerr = 0; lastup = 0; buffil = 0; Nbuf = 0;
	
	int sec = (ti.tv_sec % 86400L) / 60L;
	testart->SetText(Form("%02d:%02d", sec / 60, sec % 60));
	testop->SetText("");
	tbconn->SetEnabled(kFALSE);
	cbbits->SetEnabled(kFALSE);
	tepre->SetEnabled(kFALSE);
	tbstart->SetText("PAUSE");
	tbstop->SetEnabled(kTRUE);
	tetot->SetText("0");
	teerr->SetText("0");
	teeri->SetText("");
	teers->SetText("");
	tedti->SetText("");
	tedts->SetText("");
	pbrate->Reset();
	pbrate->SetBarColor("green");
	pbdead->Reset();
	pbdead->SetBarColor("red");
	pbbuff->Reset();
	pbbuff->SetBarColor("orange");
	
	kill(cproc, SIGUSR1);
	return;
}

void MyMainFrame::Pause() {
	istat = STAT_PAUS;
	testat->SetText(stat[istat]);
	
	kill(cproc, SIGUSR2);
	
	char buffer[1000];
	int N = Query(requester, "stop", 5, buffer, 999, QTYPE_BUF);
	if(N < 0) {
		lout->SetText("Server connection failed");
		PiDisconnect();
		return;
	}
	buffer[N] = '\0';
	printf("[parent]  STOP -> %s\n", buffer);
	
	gettimeofday(&tp, NULL);
	
	tbstart->SetText("RESUME");
	teeri->SetText("");
	tedti->SetText("");
	pbrate->SetBarColor("gray");
	pbdead->SetBarColor("gray");
	pbbuff->SetBarColor("gray");
	return;
}

void MyMainFrame::Resume() {
	istat = STAT_STRT;
	testat->SetText(stat[istat]);
	
	struct timeval tres, td;
	char buffer[1000];
	int N = Query(requester, "start", 6, buffer, 999, QTYPE_BUF);
	if(N < 0) {
		lout->SetText("Server connection failed");
		PiDisconnect();
		return;
	}
	else {
		buffer[N] = '\0';
		printf("[parent] START -> %s\n", buffer);
	}
	gettimeofday(&tres, NULL);
	timersub(&tres, &tp, &td);
	tpaused += ((uint64_t)(td.tv_sec) * 1000000L + (uint64_t)(td.tv_usec));
	
	tbstart->SetText("PAUSE");
	pbrate->SetBarColor("green");
	pbdead->SetBarColor("red");
	pbbuff->SetBarColor("orange");
	
	kill(cproc, SIGUSR1);
	return;
}

void MyMainFrame::Stop() {
	istat = STAT_STOP;
	testat->SetText(stat[istat]);
	
	kill(cproc, SIGUSR2);
	
	char buffer[1000];
	int N = Query(requester, "stop", 5, buffer, 999, QTYPE_BUF);
	if(N < 0) {
		lout->SetText("Server connection failed");
		PiDisconnect();
		return;
	}
	buffer[N] = '\0';
	printf("[parent]  STOP -> %s\n", buffer);
	
	struct timeval tf;
	gettimeofday(&tf, NULL);
	int sec = (tf.tv_sec % 86400L) / 60L;
	testop->SetText(Form("%02d:%02d", sec / 60, sec % 60));
	
	if(hbkg) hbkg->Delete();
	
	if(fout && (fout->IsZombie() == kFALSE)) {
		sprintf(buffer, "%s%05d.root", tepre->GetText(), fcnt);
		lout->SetText(Form("Last output file was %s", buffer));
		
		fout->Write();
		fout->Purge();
		fout->Close();
	}
	if(fout) delete fout;
	fout = nullptr;
	
	tbconn->SetEnabled(kTRUE);
	tbstart->SetText("START");
	tbstop->SetEnabled(kFALSE);
	teupt->SetText("");
	teeri->SetText("");
	tedti->SetText("");
	pbrate->SetPosition(4);
	pbrate->SetBarColor("gray");
	pbdead->SetPosition(1);
	pbdead->SetBarColor("gray");
	pbbuff->SetPosition(1);
	pbbuff->SetBarColor("gray");
	return;
}

void MyMainFrame::Fetch() {
	if(istat != STAT_STRT) {
		printf("[parent] Data fetching not expected in status \"%s\"\n", stat[istat]);
		kill(cproc, SIGUSR2);
		return;
	}
	
	struct Silevent data[SIZE];
	struct timeval tf, td;
	
	int N = Query(requester, "send", 5, data, SIZE * sizeof(struct Silevent), QTYPE_DAT);
	if(N < 0) {
		lout->SetText("Server connection failed");
		PiDisconnect();
		return;
	}
	
	if(N % sizeof(struct Silevent)) {
		printf("[parent] read fraction of event (size = %d)\n", N);
	}
	N /= sizeof(struct Silevent);
	buffil += (double)N;
	Nbuf += 1;
	
	ts = 0; dt = 0;
	for(int j = 0; j < N; j++) {
		if(t0 == 0L) {
			if(data[j].ts > 100L) t0 = 1L;
			continue;
		}
		else if(t0 == 1L) {
			if(data[j].ts > 100L) {
				t0 = data[j].ts + (uint64_t)(data[j].dt);
				lastts = t0;
			}
			continue;
		}
		
		tdead += (uint64_t)(data[j].dt / 1000L);
		
		hspe->Fill(data[j].val);
		hdead->Fill((double)(data[j].dt) / 1000.);
		hrate->Fill(((double)(data[j].ts - lastts)) / 1000000.);
		Nev++;
		if(data[j].emask) Nerr++;
		lastts = data[j].ts;
		
		ts = data[j].ts; dt = data[j].dt;
		val = data[j].val; emask = data[j].emask;
		tree->Fill();
	}
	if(ts) {
		tall = (ts + (uint64_t)dt - t0) / 1000L - tpaused;
	}
	
	gettimeofday(&tf, NULL);
	timersub(&tf, &ti, &td);
	uint64_t msec = ((uint64_t)td.tv_usec + 1000000L * (uint64_t)td.tv_sec + 500L) / 1000L - (uint64_t)(tpaused / 1000L);
	if(msec - lastup >= HISTUP) {
		hspe->SetBinContent(1, ((double)tall) / 100000.);
		hspe->SetBinContent(2, ((double)(tall - tdead)) / 100000.);
		fEcanvas->GetCanvas()->Modified();
		fEcanvas->GetCanvas()->Update();
		fMini[1]->GetCanvas()->Modified();
		fMini[1]->GetCanvas()->Update();
		fMini[2]->GetCanvas()->Modified();
		fMini[2]->GetCanvas()->Update();
		
		double dead = (tall == lasttall) ? 0 : ((double)(tdead - lasttdead)) / ((double)(tall - lasttall));
		double buff = Nbuf ? buffil / (Nbuf * (double)SIZE) : 0;
		double rate = 1000. * ((double)(Nev - lastN)) / ((double)(msec - lastup));
		double sec = (double)(tf.tv_sec % 86400L);
		double grange = (sec < 600.) ? 600. : sec;
		hbkg->GetXaxis()->SetRangeUser(grange - 600., grange);
		
		teupt->SetText(Form("%luh %02lum %02lus", msec / 3600000L, (msec % 3600000L) / 60000L, (msec % 60000L) / 1000L));
		tetot->SetText(Form("%lu", Nev));
		teerr->SetText(Form("%lu", Nerr));
		teeri->SetText(Form("%.0lf ev/s", rate));
		teers->SetText(Form("%.0lf ev/s", 1000. * ((double)Nev) / ((double)msec)));
		tedti->SetText(Form("%5.1lf %%", 100. * dead));
		if(tall > 0) tedts->SetText(Form("%5.1lf %%", 100. * ((double)tdead) / ((double)tall)));
		
		pbdead->Reset();
		pbdead->SetPosition(dead);
		pbbuff->Reset();
		pbbuff->SetPosition(buff);
		pbrate->Reset();
		if(rate) pbrate->SetPosition(log10(1 + rate));
		else pbrate->SetPosition(0);
		
		fMini[0]->GetCanvas()->cd();
		gall->AddPoint(sec, rate / (1. - dead));
		glive->AddPoint(sec, rate);
		gall->Draw("L");
		glive->Draw("L same");
		fMini[0]->GetCanvas()->Modified();
		fMini[0]->GetCanvas()->Update();
		
		lastN  = Nev;
		lastup = msec;
		lasttall = tall;
		lasttdead = tdead;
		buffil  = 0; Nbuf = 0;
		
		fout->Write();
		fout->Purge();
	}
	return;
}

void MyMainFrame::Test() {
	if(tbtest->IsOn()) {
		tehost->SetEnabled(kFALSE);
		fTest = true;
	}
	else {
		tehost->SetEnabled(kTRUE);
		fTest = false;
	}
	return;
}

MyMainFrame::MyMainFrame(const TGWindow *p, UInt_t w, UInt_t h) {
	me        = this;
	istat     = STAT_NCFG;
	fTest     = false;
	fPause    = false;
	context   = nullptr;
	requester = nullptr;
	fout      = nullptr;
	hbkg      = nullptr;
	qtype     = 0;
	fcnt      = 0;
	
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
				tehost->Resize(188, 24);
				tehost->SetAlignment(kTextLeft);
				hf21->AddFrame(tehost, new TGLayoutHints(kLHintsCenterY, 1, 2, 2, 2));
				
				tbtest = new TGCheckButton(hf21, "Test mode  ");
// 				tbtest->SetText("Test mode");
				tbtest->SetFont(font_sml);
				tbtest->SetEnabled(kTRUE);
				tbtest->Connect("Clicked()", "MyMainFrame", this, "Test()");
				hf21->AddFrame(tbtest, new TGLayoutHints(kLHintsCenterY, 2, 2, 2, 2));
			}
			//hf21 ends
			vf20->AddFrame(hf21, new TGLayoutHints(kLHintsExpandX|kLHintsCenterX|kLHintsCenterY, 2, 2, 2, 2));
			
			tbconn = new TGTextButton(vf20, "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\nAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\nAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA");
			tbconn->SetFont(font_big);
			tbconn->Connect("Clicked()", "MyMainFrame", this, "Connect()");
			vf20->AddFrame(tbconn, new TGLayoutHints(kLHintsCenterY, 4, 4, 4, 12));
			
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
				cbbits->SetEnabled(kFALSE);
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
				tepre->SetEnabled(kFALSE);
				hf23->AddFrame(tepre, new TGLayoutHints(kLHintsCenterY, 1, 2, 2, 2));
			}
			//hf23 ends
			vf20->AddFrame(hf23, new TGLayoutHints(kLHintsExpandX|kLHintsCenterX|kLHintsCenterY, 2, 2, 2, 2));
			
			lout = new TGLabel(vf20, "Ready to connect!");
			lout->SetTextFont(font_sml);
			vf20->AddFrame(lout, new TGLayoutHints(kLHintsCenterY|kLHintsExpandX, 2, 2, 20, 20));
			
			//hf24 starts
			TGHorizontalFrame *hf24=new TGHorizontalFrame(vf20);
			{
				TGLabel *lstat = new TGLabel(hf24, "DAQ status");
				lstat->SetTextFont(font_big);
				hf24->AddFrame(lstat, new TGLayoutHints(kLHintsCenterY|kLHintsExpandX, 2, 1, 2, 2));
				
				testat = new TGTextEntry(hf24, stat[istat]);
				testat->SetFont(font_big);
				testat->Resize(300, 32);
				testat->SetEnabled(kFALSE);
				testat->SetAlignment(kTextCenterX);
				hf24->AddFrame(testat, new TGLayoutHints(kLHintsCenterY, 1, 2, 2, 2));
			}
			//hf24 ends
			vf20->AddFrame(hf24, new TGLayoutHints(kLHintsExpandX|kLHintsCenterX|kLHintsCenterY, 2, 2, 10, 2));
			
			//hf25 starts
			TGHorizontalFrame *hf25=new TGHorizontalFrame(vf20);
			{
				tbstart = new TGTextButton(hf25, "AAAAAAAAllllllllllllllllll\nA");
				tbstart->SetFont(font_big);
				tbstart->SetEnabled(kFALSE);
				tbstart->Connect("Clicked()", "MyMainFrame", this, "MultiButton()");
				hf25->AddFrame(tbstart, new TGLayoutHints(0, 2, 1, 2, 0));
				
				tbstop = new TGTextButton(hf25, "AAAAAAAAllllllllllllllllll\nA");
				tbstop->SetFont(font_big);
				tbstop->SetEnabled(kFALSE);
				tbstop->Connect("Clicked()", "MyMainFrame", this, "Stop()");
				hf25->AddFrame(tbstop, new TGLayoutHints(0, 1, 2, 2, 1));
			}
			//hf25 ends
			vf20->AddFrame(hf25, new TGLayoutHints(kLHintsExpandX|kLHintsCenterX, 2, 2, 2, 0));
			
			//hf25bis starts
			TGHorizontalFrame *hf25bis=new TGHorizontalFrame(vf20);
			{
				testart = new TGTextEntry(hf25bis, "");
				testart->SetFont(font_big);
				testart->Resize(226, 56);
				testart->SetEnabled(kFALSE);
				testart->SetAlignment(kTextCenterX);
				hf25bis->AddFrame(testart, new TGLayoutHints(0, 2, 1, 0, 2));
				
				testop = new TGTextEntry(hf25bis, "");
				testop->SetFont(font_big);
				testop->Resize(226, 56);
				testop->SetEnabled(kFALSE);
				testop->SetAlignment(kTextCenterX);
				hf25bis->AddFrame(testop, new TGLayoutHints(0, 1, 2, 0, 2));
			}
			//hf25bis ends
			vf20->AddFrame(hf25bis, new TGLayoutHints(kLHintsExpandX|kLHintsCenterX, 2, 2, 0, 20));
			
			//hf25ter starts
			TGHorizontalFrame *hf25ter=new TGHorizontalFrame(vf20);
			{
				TGLabel *lupt = new TGLabel(hf25ter, "Uptime");
				lupt->SetTextFont(font_sml);
				hf25ter->AddFrame(lupt, new TGLayoutHints(kLHintsCenterY|kLHintsExpandX, 2, 1, 2, 2));
				
				teupt = new TGTextEntry(hf25ter, "");
				teupt->SetFont(font_sml);
				teupt->Resize(300, 24);
				teupt->SetEnabled(kFALSE);
				teupt->SetAlignment(kTextCenterX);
				hf25ter->AddFrame(teupt, new TGLayoutHints(kLHintsCenterY, 1, 2, 2, 2));
			}
			//hf25ter ends
			vf20->AddFrame(hf25ter, new TGLayoutHints(kLHintsExpandX|kLHintsCenterX, 2, 2, 2, 2));
			
			//hf25quater starts
			TGHorizontalFrame *hf25quater=new TGHorizontalFrame(vf20);
			{
				TGLabel *lnev = new TGLabel(hf25quater, "Events (tot / err)");
				lnev->SetTextFont(font_sml);
				hf25quater->AddFrame(lnev, new TGLayoutHints(kLHintsCenterY|kLHintsExpandX, 2, 1, 2, 2));
				
				tetot = new TGTextEntry(hf25quater, "");
				tetot->SetFont(font_sml);
				tetot->Resize(149, 24);
				tetot->SetEnabled(kFALSE);
				tetot->SetAlignment(kTextRight);
				hf25quater->AddFrame(tetot, new TGLayoutHints(kLHintsCenterY, 1, 1, 2, 2));
				
				teerr = new TGTextEntry(hf25quater, "");
				teerr->SetFont(font_sml);
				teerr->Resize(149, 24);
				teerr->SetEnabled(kFALSE);
				teerr->SetAlignment(kTextRight);
				hf25quater->AddFrame(teerr, new TGLayoutHints(kLHintsCenterY, 1, 2, 2, 2));
			}
			//hf25quater ends
			vf20->AddFrame(hf25quater, new TGLayoutHints(kLHintsExpandX|kLHintsCenterX, 2, 2, 2, 2));
			
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
			
			//hf26bis starts
			TGHorizontalFrame *hf26bis=new TGHorizontalFrame(vf20);
			{
				TGLabel *levs = new TGLabel(hf26bis, "Ev. rate (inst / intg)");
				levs->SetTextFont(font_sml);
				hf26bis->AddFrame(levs, new TGLayoutHints(kLHintsCenterY|kLHintsExpandX, 2, 1, 2, 2));
				
				teeri = new TGTextEntry(hf26bis, "");
				teeri->SetFont(font_sml);
				teeri->Resize(149, 24);
				teeri->SetEnabled(kFALSE);
				teeri->SetAlignment(kTextRight);
				hf26bis->AddFrame(teeri, new TGLayoutHints(kLHintsCenterY, 1, 1, 2, 2));
				
				teers = new TGTextEntry(hf26bis, "");
				teers->SetFont(font_sml);
				teers->Resize(149, 24);
				teers->SetEnabled(kFALSE);
				teers->SetAlignment(kTextRight);
				hf26bis->AddFrame(teers, new TGLayoutHints(kLHintsCenterY, 1, 2, 2, 2));
			}
			//hf26bis ends
			vf20->AddFrame(hf26bis, new TGLayoutHints(kLHintsExpandX|kLHintsCenterX, 2, 2, 2, 2));
			
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
			
			//hf27bis starts
			TGHorizontalFrame *hf27bis=new TGHorizontalFrame(vf20);
			{
				TGLabel *ldt = new TGLabel(hf27bis, "Dead time (inst / intg)");
				ldt->SetTextFont(font_sml);
				hf27bis->AddFrame(ldt, new TGLayoutHints(kLHintsCenterY|kLHintsExpandX, 2, 1, 2, 2));
				
				tedti = new TGTextEntry(hf27bis, "");
				tedti->SetFont(font_sml);
				tedti->Resize(149, 24);
				tedti->SetEnabled(kFALSE);
				tedti->SetAlignment(kTextRight);
				hf27bis->AddFrame(tedti, new TGLayoutHints(kLHintsCenterY, 1, 1, 2, 2));
				
				tedts = new TGTextEntry(hf27bis, "");
				tedts->SetFont(font_sml);
				tedts->Resize(149, 24);
				tedts->SetEnabled(kFALSE);
				tedts->SetAlignment(kTextRight);
				hf27bis->AddFrame(tedts, new TGLayoutHints(kLHintsCenterY, 1, 2, 2, 2));
			}
			//hf27bis ends
			vf20->AddFrame(hf27bis, new TGLayoutHints(kLHintsExpandX|kLHintsCenterX, 2, 2, 2, 2));
			
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
	tbstart->SetText("START");
	tbstop->SetText("STOP");
// 	tbmanu->SetText("Manual update");
// 	tbauto->SetText("AUTO update");
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
	PiDisconnect();
	usleep(10000);
	kill(cproc, SIGTERM);
	usleep(10000);
	
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
			go  = true;
			fen = false;
			signal(SIGINT,  handlesig_child);
			signal(SIGTERM, handlesig_child);
			signal(SIGUSR1, handlesig_child);
			signal(SIGUSR2, handlesig_child);
			for(;go;) {
				usleep(FETCHINT);
				if(fen) kill(pproc, SIGUSR1);
			}
			printf("[child ] End of process\n");
			return 0;
		}
		else { //processo genitore
			signal(SIGINT,  handlesig_parent);
			signal(SIGTERM, handlesig_parent);
			signal(SIGUSR1, handlesig_parent);
			TApplication theApp("App", &argc, argv);
			new MyMainFrame(gClient->GetRoot(), WINDOWX, WINDOWY);
			theApp.Run();
		}
	}
	return 0;
}

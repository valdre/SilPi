/*******************************************************************************
*                                                                              *
*                         Simone Valdre' - 14/10/2022                          *
*                  distributed under GPL-3.0-or-later licence                  *
*                                                                              *
*******************************************************************************/
#ifndef SILCLIROOT
#define SILCLIROOT

#include <TQObject.h>
#include <RQ_OBJECT.h>
#include <TGTextEntry.h>
#include <TGComboBox.h>
#include <TGLabel.h>
#include <TGProgressBar.h>
#include <TString.h>
#include <TROOT.h>
#include <TStyle.h>
#include <TApplication.h>
#include <TGClient.h>
#include <TGButton.h>
#include <TGTextBuffer.h>
#include <TRootEmbeddedCanvas.h>

#include <TFile.h>
#include <TCanvas.h>
#include <TTree.h>
#include <TH1.h>
#include <TH2.h>
#include <TGraph.h>

#define STAT_NCFG 0
#define STAT_STOP 1
#define STAT_STRT 2
#define STAT_PAUS 3

#define QTYPE_BUF 1
#define QTYPE_DAT 2

class TGWindow;
class TGMainFrame;

class MyMainFrame {
	RQ_OBJECT("MyMainFrame")
private:
	TGMainFrame *fMain;
	TRootEmbeddedCanvas *fEcanvas, *fMini[3];
	TGTextEntry *tehost, *tepre, *testat, *testart, *testop, *teupt, *tetot, *teerr;
	TGTextEntry *teeri, *teers, *tedti, *tedts;
	TGComboBox *cbbits;
	TGTextButton *tbconn, *tbstart, *tbstop;
	TGCheckButton *tbtest;
// 	TGTextButton *tbmanu;
// 	TGCheckButton *tbauto;
	TGLabel *lout;
	TGHProgressBar *pbdead, *pbbuff, *pbrate;
	
	const char stat[4][15] = {"not connected", "STOPPED", "RUNNING", "PAUSED"};
	int istat, fcnt;
	TFile *fout;
	TTree *tree;
	TH1D *hspe, *hdead, *hrate;
	TH2F *hbkg;
	TGraph *gall, *glive;
	
	bool fTest, fPause;
	void *context, *requester;
	int qtype;
	
	uint64_t t0, lastts, tall, tdead, lasttall, lasttdead, lastN;
	uint64_t Nev, Nerr, lastup;
	uint64_t tpaused;
	double buffil, Nbuf;
	struct timeval ti, tp;
	
	int Query(void *requester, const void *q, const size_t &qlen, void *ans, const size_t &alen, const int &type);
	void SetupTree();
	void SetupHistos();
	void Start();
	void Pause();
	void Resume();
	void PiDisconnect();
	
public:
	MyMainFrame(const TGWindow *p, UInt_t w, UInt_t h);
	void Connect();
	void MultiButton();
	void Stop();
	void Fetch();
	void Terminate();
	void Test();
	
	virtual ~MyMainFrame();
};

#endif

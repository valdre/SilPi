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

class TGWindow;
class TGMainFrame;

class MyMainFrame {
	RQ_OBJECT("MyMainFrame")
private:
	TGMainFrame *fMain;
	TRootEmbeddedCanvas *fEcanvas, *fMini[3];
	TGTextEntry *tehost, *tepre, *testat;
	TGComboBox *cbbits;
	TGTextButton *tbconn, *tbstart, *tbstop, *tbmanu;
	TGCheckButton *tbauto;
	TGLabel *lout;
	TGHProgressBar *pbdead, *pbbuff, *pbrate;
	
	const char stat[4][15] = {"not connected", "STOPPED", "RUNNING", "PAUSED"};
	int istat, range = 0;
	TFile *fout;
	TTree *tree;
	TH1D *hspe, *hdead, *hrate;
	TH2F *hbkg;
	TGraph *gall, *glive;
	
	void *context, *requester;
	
	uint64_t t0, lastts, tall, tdead, lasttall, lasttdead, lastN;
	uint64_t Nev, lastup;
	double buffil, Nbuf;
	struct timeval ti;
	int count;
	
	void SetupTree();
	void SetupHistos();
	
public:
	MyMainFrame(const TGWindow *p, UInt_t w, UInt_t h);
	void Connect();
	void Start();
	void Stop();
	void Fetch();
	void Terminate();
	void ManUpdate();
	
	virtual ~MyMainFrame();
};

#endif

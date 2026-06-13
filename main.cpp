// RasStudio - Android Studio Clone
// Pure Win32 API + GDI+
// Compile: g++ main.cpp -o RasStudio.exe -lgdi32 -lgdiplus -lcomctl32 -lshell32 -lole32 -mwindows -std=c++17

#define UNICODE
#define _UNICODE
#define _WIN32_WINNT 0x0601
#include <windows.h>
#include <windowsx.h>
#include <gdiplus.h>
#include <commctrl.h>
#include <shlobj.h>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <functional>
#include <fstream>
#include <sstream>
#include <atomic>
#include <algorithm>
#pragma comment(lib,"gdiplus.lib")
#pragma comment(lib,"comctl32.lib")
#pragma comment(lib,"shell32.lib")
#pragma comment(lib,"ole32.lib")
using namespace Gdiplus;

// ════════════════════════════════════════
//  COLORS  (Android Studio dark theme)
// ════════════════════════════════════════
#define C(r,g,b)      Color(255,r,g,b)
#define CA(a,r,g,b)   Color(a,r,g,b)
static const Color
  cBG          = C(43,43,43),
  cTitleBar    = C(50,50,50),    // ← same as Android Studio header
  cSidebar     = C(60,63,65),
  cSidebarHdr  = C(75,75,75),
  cPanel       = C(43,43,43),
  cPanel2      = C(49,51,53),
  cBorder      = C(30,30,30),
  cAccent      = C(75,110,175),  // AS blue
  cAccentHov   = C(95,130,195),
  cGreen       = C(98,151,85),
  cYellow      = C(204,120,50),
  cRed         = C(188,63,60),
  cCyan        = C(104,151,187),
  cPurple      = C(150,118,198),
  cText        = C(169,183,198),
  cTextBright  = C(214,220,228),
  cTextDim     = C(100,107,115),
  cTermBG      = C(30,31,32),
  cTermHdr     = C(37,37,38),
  cSelBG       = C(33,66,131),
  cHovBG       = C(62,66,72),
  cTabActive   = C(43,43,43),
  cTabInactive = C(55,58,60),
  cLineNum     = C(56,60,74),
  cGutter      = C(49,51,53),
  cRunGreen    = C(65,168,43),
  cDebugYellow = C(200,145,0),
  cCloseRed    = C(196,43,28);

// ════════════════════════════════════════
//  GLOBALS
// ════════════════════════════════════════
HWND  g_hwnd   = nullptr;
float g_scale  = 1.0f;
int   g_W=1440, g_H=900;
bool  g_maximized = false;
bool  g_dragging  = false;
POINT g_dragPt    = {};

// layout
float TH=38, BH=48, SBW=250, SBHW=22, TBH=36, TERMH_FRAC=0.28f, STATUSH=24;
// split dragging
bool  g_splitDrag=false;
float g_splitY=0;

// ADB
std::wstring g_adbStatus=L"No device";
Color        g_adbCol   =cRed;
std::atomic<bool> g_adbBusy{false};

// Project
std::wstring g_projectPath;

// Terminal
std::wstring  g_termText = L"RasStudio ready.\r\nOpen a project folder to begin.\r\n";
std::mutex    g_termMtx;
std::atomic<bool> g_building{false};
HANDLE        g_buildProc=nullptr;

// File tree
struct FNode {
    std::wstring name, path;
    bool isDir, expanded;
    int  depth;
    std::vector<FNode> children;
};
std::vector<FNode>  g_tree;
std::vector<FNode*> g_flat;
int g_treeHov=-1, g_treeSel=-1, g_treeScroll=0;

// Editor
struct Tab {
    std::wstring name,path,content;
    bool modified=false;
};
std::vector<Tab> g_tabs;
int g_activeTab=-1, g_tabHov=-1;
int g_edScroll=0;
HWND g_editCtrl=nullptr;

// Hover states for toolbar buttons
enum BtnId { BID_FOLDER,BID_DEBUG,BID_RELEASE,BID_INSTALL,BID_STOP,BID_ADB,BID_CLOSE,BID_MAX,BID_MIN, BID_COUNT };
bool g_btnHov[BID_COUNT]={};

// Rects (logical coords)
RectF rTitle,rBtnBar,rSidebar,rTabBar,rEditor,rTerm,rStatus;
RectF rBtnClose,rBtnMax,rBtnMin;
RectF rBtnFolder,rBtnDebug,rBtnRelease,rBtnInstall,rBtnStop,rBtnAdb;

// ════════════════════════════════════════
//  FORWARD DECLS
// ════════════════════════════════════════
LRESULT CALLBACK WndProc(HWND,UINT,WPARAM,LPARAM);
void RecalcLayout();
void DrawAll(HDC);
void DrawTitleBar(Graphics&);
void DrawBtnBar(Graphics&);
void DrawSidebar(Graphics&);
void DrawTabBar(Graphics&);
void DrawEditor(Graphics&);
void DrawTerminal(Graphics&);
void DrawStatusBar(Graphics&);
void RoundRect2(Graphics&,RectF,float,Color,Color=Color(0,0,0,0),float=0);
void Txt(Graphics&,const std::wstring&,Font&,RectF,Color,StringAlignment=StringAlignmentNear,StringAlignment=StringAlignmentCenter);
bool Hit(RectF r,float x,float y);
void AppendTerm(const std::wstring&);
void RunCmd(const std::wstring&,const std::wstring&);
void LoadTree(const std::wstring&,std::vector<FNode>&,int);
void FlattenTree(std::vector<FNode>&,std::vector<FNode*>&);
void OpenTab(const std::wstring&);
void BrowseFolder();
void CheckAdb();
void BuildAPK(bool);
void InstallAPK();
void StopBuild();

// ════════════════════════════════════════
//  STRING HELPERS
// ════════════════════════════════════════
std::wstring S2W(const std::string& s){
    if(s.empty())return L"";
    int n=MultiByteToWideChar(CP_UTF8,0,s.c_str(),-1,nullptr,0);
    std::wstring r(n,0); MultiByteToWideChar(CP_UTF8,0,s.c_str(),-1,&r[0],n); return r;
}
std::string W2S(const std::wstring& s){
    if(s.empty())return "";
    int n=WideCharToMultiByte(CP_UTF8,0,s.c_str(),-1,nullptr,0,nullptr,nullptr);
    std::string r(n,0); WideCharToMultiByte(CP_UTF8,0,s.c_str(),-1,&r[0],n,nullptr,nullptr); return r;
}

// ════════════════════════════════════════
//  WINMAIN
// ════════════════════════════════════════
int WINAPI WinMain(HINSTANCE hi,HINSTANCE,LPSTR,int){
    SetProcessDPIAware();
    HDC hdc0=GetDC(nullptr);
    g_scale=(float)GetDeviceCaps(hdc0,LOGPIXELSX)/96.f;
    ReleaseDC(nullptr,hdc0);

    GdiplusStartupInput gsi; ULONG_PTR tok;
    GdiplusStartup(&tok,&gsi,nullptr);

    INITCOMMONCONTROLSEX icc={sizeof(icc),ICC_WIN95_CLASSES};
    InitCommonControlsEx(&icc);

    WNDCLASSEX wc={sizeof(wc)};
    wc.lpfnWndProc=WndProc; wc.hInstance=hi;
    wc.hCursor=LoadCursor(nullptr,IDC_ARROW);
    wc.hbrBackground=(HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName=L"RasStudio";
    wc.hIcon=LoadIcon(nullptr,IDI_APPLICATION);
    RegisterClassEx(&wc);

    int sw=GetSystemMetrics(SM_CXSCREEN),sh=GetSystemMetrics(SM_CYSCREEN);
    g_hwnd=CreateWindowEx(WS_EX_APPWINDOW,L"RasStudio",L"RasStudio",
        WS_POPUP|WS_THICKFRAME|WS_MINIMIZEBOX,
        (sw-(int)(g_W*g_scale))/2,(sh-(int)(g_H*g_scale))/2,
        (int)(g_W*g_scale),(int)(g_H*g_scale),
        nullptr,nullptr,hi,nullptr);

    g_editCtrl=CreateWindowEx(0,L"EDIT",L"",
        WS_CHILD|WS_VSCROLL|ES_MULTILINE|ES_AUTOVSCROLL|ES_WANTRETURN,
        0,0,0,0,g_hwnd,(HMENU)101,hi,nullptr);
    // Apply Consolas font to edit
    HFONT hf=CreateFont((int)(14*g_scale),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,
        DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,
        FIXED_PITCH,L"Consolas");
    SendMessage(g_editCtrl,WM_SETFONT,(WPARAM)hf,TRUE);

    RecalcLayout();
    ShowWindow(g_hwnd,SW_SHOW);
    UpdateWindow(g_hwnd);

    std::thread([](){CheckAdb();}).detach();

    MSG msg;
    while(GetMessage(&msg,nullptr,0,0)){
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    GdiplusShutdown(tok);
    return 0;
}

// ════════════════════════════════════════
//  LAYOUT
// ════════════════════════════════════════
void RecalcLayout(){
    float W=(float)g_W, H=(float)g_H;
    float termH = (H-TH-BH-STATUSH)*TERMH_FRAC;
    float editH = (H-TH-BH-TBH-STATUSH) - termH;

    rTitle  ={0,0,W,TH};
    rBtnBar ={0,TH,W,BH};
    rSidebar={0,TH+BH,SBW,H-TH-BH-STATUSH};
    rTabBar ={SBW,TH+BH,W-SBW,TBH};
    rEditor ={SBW,TH+BH+TBH,W-SBW,editH};
    rTerm   ={SBW,TH+BH+TBH+editH,W-SBW,termH};
    rStatus ={0,H-STATUSH,W,STATUSH};

    // title buttons
    float by=(TH-26)/2.f;
    rBtnClose={W-46,by,36,26};
    rBtnMax  ={W-88,by,36,26};
    rBtnMin  ={W-130,by,36,26};

    // toolbar buttons
    float bpad=8,bh2=BH-12,by2=TH+6;
    rBtnFolder ={bpad,      by2,120,bh2};
    rBtnDebug  ={bpad+128,  by2,130,bh2};
    rBtnRelease={bpad+266,  by2,140,bh2};
    rBtnInstall={bpad+414,  by2,130,bh2};
    rBtnStop   ={bpad+552,  by2,80, bh2};
    rBtnAdb    ={W-210,     by2,200,bh2};

    if(g_editCtrl){
        int ex=(int)(SBW*g_scale),ey=(int)((TH+BH+TBH)*g_scale);
        int ew=(int)((W-SBW)*g_scale),eh=(int)(editH*g_scale);
        SetWindowPos(g_editCtrl,nullptr,ex,ey,ew,eh,SWP_NOZORDER);
    }
}

// ════════════════════════════════════════
//  DRAW HELPERS
// ════════════════════════════════════════
void RoundRect2(Graphics& g,RectF r,float rad,Color fill,Color border,float bw){
    GraphicsPath p;
    p.AddArc(r.X,r.Y,rad*2,rad*2,180,90);
    p.AddArc(r.X+r.Width-rad*2,r.Y,rad*2,rad*2,270,90);
    p.AddArc(r.X+r.Width-rad*2,r.Y+r.Height-rad*2,rad*2,rad*2,0,90);
    p.AddArc(r.X,r.Y+r.Height-rad*2,rad*2,rad*2,90,90);
    p.CloseFigure();
    SolidBrush fb(fill); g.FillPath(&fb,&p);
    if(bw>0&&border.GetA()>0){Pen pen(border,bw);g.DrawPath(&pen,&p);}
}
void Txt(Graphics& g,const std::wstring& t,Font& f,RectF r,Color c,StringAlignment ha,StringAlignment va){
    SolidBrush b(c); StringFormat sf;
    sf.SetAlignment(ha); sf.SetLineAlignment(va);
    sf.SetFormatFlags(StringFormatFlagsNoWrap);
    sf.SetTrimming(StringTrimmingEllipsisCharacter);
    g.DrawString(t.c_str(),-1,&f,r,&sf,&b);
}
bool Hit(RectF r,float x,float y){return x>=r.X&&x<r.X+r.Width&&y>=r.Y&&y<r.Y+r.Height;}

// toolbar button helper
void DrawToolBtn(Graphics& g,RectF r,const std::wstring& lbl,Color col,bool hov,bool disabled=false){
    Color c=disabled?C(55,58,60):hov?Color(min(255,(int)col.GetR()+25),min(255,(int)col.GetG()+25),min(255,(int)col.GetB()+25),col.GetA()):col;
    if(disabled)c=C(55,58,60);
    RoundRect2(g,r,5,c,CA(0,0,0,0),0);
    Font f(L"Segoe UI",8,FontStyleBold);
    Color tc=disabled?cTextDim:cTextBright;
    Txt(g,lbl,f,r,tc,StringAlignmentCenter,StringAlignmentCenter);
}

// ════════════════════════════════════════
//  TITLE BAR
// ════════════════════════════════════════
void DrawTitleBar(Graphics& g){
    SolidBrush bg(cTitleBar); g.FillRectangle(&bg,rTitle);

    // App icon + name
    RoundRect2(g,{10,8,22,22},4,cAccent,CA(0,0,0,0),0);
    Font fLogo(L"Segoe UI",7,FontStyleBold);
    Txt(g,L"RS",fLogo,{10,8,22,22},cTextBright,StringAlignmentCenter,StringAlignmentCenter);

    Font fName(L"Segoe UI",10,FontStyleBold);
    Txt(g,L"RasStudio",fName,{38,0,180,TH},cTextBright,StringAlignmentNear,StringAlignmentCenter);

    // Project name center
    if(!g_projectPath.empty()){
        size_t p=g_projectPath.rfind(L'\\');
        std::wstring pname=(p!=std::wstring::npos)?g_projectPath.substr(p+1):g_projectPath;
        Font fp(L"Segoe UI",9);
        Txt(g,pname,fp,{200,0,(float)g_W-400,TH},cTextDim,StringAlignmentCenter,StringAlignmentCenter);
    }

    // Min/Max/Close
    // Min
    Color cMin=g_btnHov[BID_MIN]?cHovBG:CA(0,50,50,68);
    RoundRect2(g,rBtnMin,4,cMin,CA(0,0,0,0),0);
    Font fWin(L"Segoe UI",9,FontStyleBold);
    Txt(g,L"─",fWin,rBtnMin,g_btnHov[BID_MIN]?cTextBright:cTextDim,StringAlignmentCenter,StringAlignmentCenter);

    // Max
    Color cMax=g_btnHov[BID_MAX]?cHovBG:CA(0,50,50,68);
    RoundRect2(g,rBtnMax,4,cMax,CA(0,0,0,0),0);
    Txt(g,g_maximized?L"❐":L"□",fWin,rBtnMax,g_btnHov[BID_MAX]?cTextBright:cTextDim,StringAlignmentCenter,StringAlignmentCenter);

    // Close
    Color cCl=g_btnHov[BID_CLOSE]?cCloseRed:CA(0,50,50,68);
    RoundRect2(g,rBtnClose,4,cCl,CA(0,0,0,0),0);
    Txt(g,L"✕",fWin,rBtnClose,g_btnHov[BID_CLOSE]?cTextBright:cTextDim,StringAlignmentCenter,StringAlignmentCenter);

    // bottom separator
    Pen sep(cBorder,1.f);
    g.DrawLine(&sep,0.f,TH-1,(float)g_W,TH-1);
}

// ════════════════════════════════════════
//  TOOLBAR (Build bar)
// ════════════════════════════════════════
void DrawBtnBar(Graphics& g){
    SolidBrush bg(cPanel2); g.FillRectangle(&bg,rBtnBar);
    Pen sep(cBorder,1.f);
    g.DrawLine(&sep,0.f,TH+BH-1,(float)g_W,TH+BH-1);

    DrawToolBtn(g,rBtnFolder,  L"📁  Open Folder",  C(62,65,70),   g_btnHov[BID_FOLDER]);
    DrawToolBtn(g,rBtnDebug,   L"▶  Run Debug",     C(50,130,60),  g_btnHov[BID_DEBUG],  g_building||g_projectPath.empty());
    DrawToolBtn(g,rBtnRelease, L"📦  Build Release", C(80,60,160),  g_btnHov[BID_RELEASE],g_building||g_projectPath.empty());
    DrawToolBtn(g,rBtnInstall, L"📲  Install APK",   C(30,100,180), g_btnHov[BID_INSTALL],g_projectPath.empty());
    DrawToolBtn(g,rBtnStop,    L"⏹  Stop",           g_building?C(160,40,40):C(60,60,70), g_btnHov[BID_STOP],!g_building);

    // ADB indicator
    RoundRect2(g,rBtnAdb,5,C(38,40,44),cBorder,1);
    Font fAdb(L"Segoe UI",8);
    std::wstring adbTxt=L"ADB: "+g_adbStatus;
    Txt(g,adbTxt,fAdb,rBtnAdb,g_adbCol,StringAlignmentCenter,StringAlignmentCenter);
}

// ════════════════════════════════════════
//  SIDEBAR  (Project explorer)
// ════════════════════════════════════════
void DrawSidebar(Graphics& g){
    SolidBrush bg(cSidebar); g.FillRectangle(&bg,rSidebar);
    Pen sep(cBorder,1.f);
    g.DrawLine(&sep,SBW-1,rSidebar.Y,SBW-1,rSidebar.Y+rSidebar.Height);

    // Section header
    RectF hdr={0,rSidebar.Y,SBW-1,SBHW};
    SolidBrush hbg(cSidebarHdr); g.FillRectangle(&hbg,hdr);
    Font fHdr(L"Segoe UI",7,FontStyleBold);
    Txt(g,L"  PROJECT",fHdr,hdr,cTextDim,StringAlignmentNear,StringAlignmentCenter);

    if(g_projectPath.empty()){
        Font fm(L"Segoe UI",8);
        Txt(g,L"Open a folder to see files",fm,
            {8,rSidebar.Y+SBHW+10,SBW-16,60},cTextDim,StringAlignmentNear,StringAlignmentNear);
        return;
    }

    float rowH=20, y=rSidebar.Y+SBHW-(float)g_treeScroll;
    Font ftree(L"Segoe UI",9);

    for(int i=0;i<(int)g_flat.size();i++){
        if(y+rowH<rSidebar.Y+SBHW){y+=rowH;continue;}
        if(y>rSidebar.Y+rSidebar.Height)break;
        FNode* n=g_flat[i];
        RectF row={0,y,SBW-1,rowH};

        // background
        if(i==g_treeSel){SolidBrush sb(cSelBG);g.FillRectangle(&sb,row);}
        else if(i==g_treeHov){SolidBrush hb(cHovBG);g.FillRectangle(&hb,row);}

        float indent=4.f+n->depth*14.f;
        // arrow for dir
        std::wstring arrow=n->isDir?(n->expanded?L"▼ ":L"▶ "):L"  ";
        // icon + name
        std::wstring icon;
        Color fc=cText;
        if(n->isDir){icon=L"📁 ";fc=C(204,153,51);}
        else{
            std::wstring ext;
            size_t dot=n->name.rfind(L'.');
            if(dot!=std::wstring::npos)ext=n->name.substr(dot);
            if(ext==L".kt"||ext==L".kts"){icon=L"K ";fc=cPurple;}
            else if(ext==L".xml"){icon=L"X ";fc=cGreen;}
            else if(ext==L".gradle"){icon=L"G ";fc=cYellow;}
            else if(ext==L".json"){icon=L"J ";fc=C(204,153,51);}
            else if(ext==L".md"){icon=L"M ";fc=cCyan;}
            else if(ext==L".properties"){icon=L"P ";fc=C(150,150,80);}
            else{icon=L"  ";fc=cText;}
        }
        RectF tr={indent,y,SBW-indent-4,rowH};
        Txt(g,arrow+icon+n->name,ftree,tr,fc,StringAlignmentNear,StringAlignmentCenter);
        y+=rowH;
    }
}

// ════════════════════════════════════════
//  TAB BAR
// ════════════════════════════════════════
void DrawTabBar(Graphics& g){
    SolidBrush bg(cTabInactive); g.FillRectangle(&bg,rTabBar);
    Pen sep(cBorder,1.f);
    g.DrawLine(&sep,rTabBar.X,rTabBar.Y+rTabBar.Height-1,rTabBar.X+rTabBar.Width,rTabBar.Y+rTabBar.Height-1);

    if(g_tabs.empty()){
        Font f(L"Segoe UI",8);
        Txt(g,L"  No files open",f,rTabBar,cTextDim,StringAlignmentNear,StringAlignmentCenter);
        return;
    }
    float tx=rTabBar.X, tw=160;
    Font ft(L"Segoe UI",9);
    for(int i=0;i<(int)g_tabs.size();i++){
        bool active=(i==g_activeTab);
        RectF tab={tx,rTabBar.Y,tw,rTabBar.Height};
        Color tc=active?cTabActive:cTabInactive;
        SolidBrush tb(tc); g.FillRectangle(&tb,tab);
        // active accent line top
        if(active){SolidBrush al(cAccent);g.FillRectangle(&al,{tx,rTabBar.Y,tw,2});}
        // separator
        Pen sp(cBorder,1.f);
        g.DrawLine(&sp,tx+tw-1,rTabBar.Y,tx+tw-1,rTabBar.Y+rTabBar.Height);
        // label
        std::wstring lbl=g_tabs[i].name+(g_tabs[i].modified?L" ●":L"");
        Txt(g,lbl,ft,{tx+8,rTabBar.Y,tw-28,rTabBar.Height},active?cTextBright:cTextDim,StringAlignmentNear,StringAlignmentCenter);
        // close x
        Font fx(L"Segoe UI",7);
        Txt(g,L"✕",fx,{tx+tw-20,rTabBar.Y,16,rTabBar.Height},cTextDim,StringAlignmentCenter,StringAlignmentCenter);
        tx+=tw;
    }
}

// ════════════════════════════════════════
//  EDITOR
// ════════════════════════════════════════
void DrawEditor(Graphics& g){
    SolidBrush bg(cBG); g.FillRectangle(&bg,rEditor);

    if(g_activeTab<0||(int)g_tabs.size()<=g_activeTab){
        // Welcome screen
        Font fw(L"Segoe UI",14);
        Txt(g,L"Welcome to RasStudio",fw,
            {rEditor.X,rEditor.Y,rEditor.Width,rEditor.Height/2},
            cTextDim,StringAlignmentCenter,StringAlignmentFar);
        Font fw2(L"Segoe UI",9);
        Txt(g,L"Open a project folder and select a file to edit",fw2,
            {rEditor.X,rEditor.Y+rEditor.Height/2,rEditor.Width,rEditor.Height/2},
            cTextDim,StringAlignmentCenter,StringAlignmentNear);
        return;
    }

    const std::wstring& code=g_tabs[g_activeTab].content;
    float lnW=52, pad=8, lineH=18;
    Font fcode(L"Consolas",10);
    Font fln(L"Consolas",9);

    // gutter bg
    SolidBrush gb(cGutter);
    g.FillRectangle(&gb,{rEditor.X,rEditor.Y,lnW,rEditor.Height});
    Pen gSep(cBorder,1.f);
    g.DrawLine(&gSep,rEditor.X+lnW,rEditor.Y,rEditor.X+lnW,rEditor.Y+rEditor.Height);

    // parse lines
    std::vector<std::wstring> lines;
    std::wstringstream ss(code);
    std::wstring ln;
    while(std::getline(ss,ln))lines.push_back(ln);

    float startY=rEditor.Y+pad-(float)g_edScroll;
    for(int i=0;i<(int)lines.size();i++){
        float ly=startY+i*lineH;
        if(ly+lineH<rEditor.Y)continue;
        if(ly>rEditor.Y+rEditor.Height)break;

        // line number
        RectF lnR={rEditor.X+2,ly,lnW-4,lineH};
        Txt(g,std::to_wstring(i+1),fln,lnR,cTextDim,StringAlignmentFar,StringAlignmentCenter);

        // syntax color
        const std::wstring& line=lines[i];
        Color lc=cText;
        std::wstring trim=line;
        size_t fs=trim.find_first_not_of(L" \t");
        if(fs!=std::wstring::npos)trim=trim.substr(fs);

        if(trim.find(L"//") ==0)                lc=C(106,153,85);  // comment green
        else if(trim.find(L"/*")==0||trim.find(L"*")==0) lc=C(106,153,85);
        else if(trim.find(L"import ")==0)        lc=cCyan;
        else if(trim.find(L"package ")==0)       lc=cCyan;
        else if(trim.find(L"fun ")==0)           lc=C(86,156,214);  // AS blue
        else if(trim.find(L"class ")==0||
                trim.find(L"object ")==0||
                trim.find(L"interface ")==0)     lc=C(78,201,176);  // teal
        else if(trim.find(L"val ")==0||
                trim.find(L"var ")==0)           lc=cPurple;
        else if(trim.find(L"override ")==0)      lc=C(86,156,214);
        else if(!trim.empty()&&trim[0]==L'@')    lc=C(255,198,109); // annotation
        else if(trim.find(L"return ")==0||
                trim.find(L"if(")==0||
                trim.find(L"if (")==0||
                trim.find(L"for(")==0||
                trim.find(L"when")==0||
                trim.find(L"while")==0)          lc=C(197,134,192); // keyword purple

        RectF cr={rEditor.X+lnW+pad,ly,rEditor.Width-lnW-pad-4,lineH};
        Txt(g,line,fcode,cr,lc,StringAlignmentNear,StringAlignmentCenter);
    }
}

// ════════════════════════════════════════
//  TERMINAL
// ════════════════════════════════════════
void DrawTerminal(Graphics& g){
    SolidBrush bg(cTermBG); g.FillRectangle(&bg,rTerm);
    Pen sep(cBorder,1.f);
    g.DrawLine(&sep,rTerm.X,rTerm.Y,rTerm.X+rTerm.Width,rTerm.Y);

    // Header tabs (like AS)
    float tabW=100;
    RectF thdr={rTerm.X,rTerm.Y,tabW,22};
    SolidBrush thbg(cAccent); g.FillRectangle(&thbg,thdr);
    Font fhdr(L"Segoe UI",8,FontStyleBold);
    Txt(g,L"  Build",fhdr,thdr,cTextBright,StringAlignmentNear,StringAlignmentCenter);

    RectF thdr2={rTerm.X+tabW,rTerm.Y,tabW,22};
    SolidBrush thbg2(cTermHdr); g.FillRectangle(&thbg2,thdr2);
    Txt(g,L"  Run",fhdr,thdr2,cTextDim,StringAlignmentNear,StringAlignmentCenter);

    // building indicator
    if(g_building){
        Font fi(L"Segoe UI",8,FontStyleBold);
        RectF ir={rTerm.X+rTerm.Width-130,rTerm.Y,125,22};
        Txt(g,L"⚙ Building...",fi,ir,cYellow,StringAlignmentFar,StringAlignmentCenter);
    }

    // output
    std::wstring txt;
    {std::lock_guard<std::mutex> lk(g_termMtx);txt=g_termText;}

    Font fterm(L"Consolas",9);
    float lineH=15, pad=4;
    std::vector<std::wstring> lines;
    std::wstringstream ss(txt);
    std::wstring ln;
    while(std::getline(ss,ln)){
        if(!ln.empty()&&ln.back()==L'\r')ln.pop_back();
        lines.push_back(ln);
    }

    float avail=rTerm.Height-22-pad;
    int maxL=(int)(avail/lineH);
    int start=(int)lines.size()>maxL?(int)lines.size()-maxL:0;
    float y=rTerm.Y+22+pad;

    for(int i=start;i<(int)lines.size();i++){
        if(y+lineH>rTerm.Y+rTerm.Height)break;
        const std::wstring& l=lines[i];
        Color lc=cText;
        if(l.find(L"BUILD SUCCESSFUL")!=std::wstring::npos)lc=cGreen;
        else if(l.find(L"BUILD FAILED")!=std::wstring::npos||
                l.find(L"error:")!=std::wstring::npos||
                l.find(L"ERROR")!=std::wstring::npos)lc=cRed;
        else if(l.find(L"warning:")!=std::wstring::npos)lc=cYellow;
        else if(l.find(L"> Task")!=std::wstring::npos)lc=cAccent;
        else if(l.find(L"[+]")!=std::wstring::npos||l.find(L"SUCCESS")!=std::wstring::npos)lc=cGreen;
        else if(l.find(L"[*]")!=std::wstring::npos)lc=C(150,180,255);
        else if(l.find(L"[!]")!=std::wstring::npos||l.find(L"[ERROR]")!=std::wstring::npos)lc=cRed;

        SolidBrush tb(lc);
        StringFormat sf;
        sf.SetFormatFlags(StringFormatFlagsNoWrap);
        sf.SetTrimming(StringTrimmingEllipsisCharacter);
        g.DrawString(l.c_str(),-1,&fterm,{rTerm.X+pad,y,rTerm.Width-pad*2,lineH},&sf,&tb);
        y+=lineH;
    }

    // resize handle
    Pen rh(cBorder,2.f);
    g.DrawLine(&rh,rTerm.X,rTerm.Y+1,rTerm.X+rTerm.Width,rTerm.Y+1);
    SolidBrush rhdot(cTextDim);
    for(int i=0;i<3;i++)
        g.FillEllipse(&rhdot,{rTerm.X+rTerm.Width/2-12+i*12.f,rTerm.Y-2,4,4});
}

// ════════════════════════════════════════
//  STATUS BAR
// ════════════════════════════════════════
void DrawStatusBar(Graphics& g){
    SolidBrush bg(cAccent); g.FillRectangle(&bg,rStatus);
    Font f(L"Segoe UI",8);
    std::wstring left=g_projectPath.empty()?L"  No project open":L"  "+g_projectPath;
    std::wstring right=L"Kotlin  |  UTF-8  |  RasStudio v1.0  ";
    if(g_activeTab>=0&&g_activeTab<(int)g_tabs.size())
        right=g_tabs[g_activeTab].name+L"  |  "+right;
    Txt(g,left,f,{4,rStatus.Y,(float)g_W/2,STATUSH},cTextBright,StringAlignmentNear,StringAlignmentCenter);
    Txt(g,right,f,{0,rStatus.Y,(float)g_W-4,STATUSH},cTextBright,StringAlignmentFar,StringAlignmentCenter);
}

// ════════════════════════════════════════
//  DRAW ALL
// ════════════════════════════════════════
void DrawAll(HDC hdc){
    int pw=(int)(g_W*g_scale),ph=(int)(g_H*g_scale);
    HDC mdc=CreateCompatibleDC(hdc);
    HBITMAP bmp=CreateCompatibleBitmap(hdc,pw,ph);
    HBITMAP old=(HBITMAP)SelectObject(mdc,bmp);

    Graphics gg(mdc);
    gg.SetSmoothingMode(SmoothingModeHighQuality);
    gg.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
    gg.ScaleTransform(g_scale,g_scale);

    SolidBrush bgb(cBG); gg.FillRectangle(&bgb,0.f,0.f,(float)g_W,(float)g_H);

    DrawBtnBar(gg);
    DrawSidebar(gg);
    DrawTabBar(gg);
    DrawEditor(gg);
    DrawTerminal(gg);
    DrawStatusBar(gg);
    DrawTitleBar(gg);  // on top

    BitBlt(hdc,0,0,pw,ph,mdc,0,0,SRCCOPY);
    SelectObject(mdc,old);
    DeleteObject(bmp);
    DeleteDC(mdc);
}

// ════════════════════════════════════════
//  FILE TREE
// ════════════════════════════════════════
void LoadTree(const std::wstring& path,std::vector<FNode>& nodes,int depth){
    WIN32_FIND_DATA fd;
    HANDLE h=FindFirstFile((path+L"\\*").c_str(),&fd);
    if(h==INVALID_HANDLE_VALUE)return;
    static const std::vector<std::wstring> skip={L".git",L".gradle",L".idea",L"build",L"__pycache__",L".cxx"};
    std::vector<FNode> dirs,files;
    do{
        std::wstring nm=fd.cFileName;
        if(nm==L"."||nm==L"..")continue;
        bool isD=(fd.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)!=0;
        if(isD){bool sk=false;for(auto&s:skip)if(nm==s){sk=true;break;}if(sk)continue;}
        FNode n; n.name=nm; n.path=path+L"\\"+nm;
        n.isDir=isD; n.expanded=false; n.depth=depth;
        if(isD)dirs.push_back(n); else files.push_back(n);
    }while(FindNextFile(h,&fd));
    FindClose(h);
    for(auto&d:dirs)nodes.push_back(d);
    for(auto&f:files)nodes.push_back(f);
}
void FlattenTree(std::vector<FNode>& nodes,std::vector<FNode*>& flat){
    for(auto& n:nodes){flat.push_back(&n);if(n.isDir&&n.expanded)FlattenTree(n.children,flat);}
}
void ToggleExpand(FNode& n){
    if(!n.isDir)return;
    n.expanded=!n.expanded;
    if(n.expanded&&n.children.empty())LoadTree(n.path,n.children,n.depth+1);
}
void OpenTab(const std::wstring& path){
    for(int i=0;i<(int)g_tabs.size();i++){
        if(g_tabs[i].path==path){g_activeTab=i;InvalidateRect(g_hwnd,nullptr,FALSE);return;}
    }
    std::wifstream f(path);
    if(!f.is_open())return;
    std::wstringstream ss; ss<<f.rdbuf();
    Tab t; t.path=path;
    size_t p=path.rfind(L'\\');
    t.name=(p!=std::wstring::npos)?path.substr(p+1):path;
    t.content=ss.str(); t.modified=false;
    g_tabs.push_back(t); g_activeTab=(int)g_tabs.size()-1;
    if(g_editCtrl)SetWindowText(g_editCtrl,t.content.c_str());
    InvalidateRect(g_hwnd,nullptr,FALSE);
}
void BrowseFolder(){
    BROWSEINFO bi={};
    bi.hwndOwner=g_hwnd;
    bi.lpszTitle=L"Select Android Project Folder";
    bi.ulFlags=BIF_RETURNONLYFSDIRS|BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE pid=SHBrowseForFolder(&bi);
    if(!pid)return;
    wchar_t buf[MAX_PATH];
    if(SHGetPathFromIDList(pid,buf)){
        g_projectPath=buf;
        g_tree.clear(); g_flat.clear();
        LoadTree(g_projectPath,g_tree,0);
        FlattenTree(g_tree,g_flat);
        AppendTerm(L"\r\n[*] Project: "+g_projectPath+L"\r\n");
    }
    CoTaskMemFree(pid);
    InvalidateRect(g_hwnd,nullptr,FALSE);
}

// ════════════════════════════════════════
//  TERMINAL / PROCESS
// ════════════════════════════════════════
void AppendTerm(const std::wstring& t){
    std::lock_guard<std::mutex> lk(g_termMtx);
    g_termText+=t;
    // trim to last 2000 lines
    int cnt=0; size_t pos=g_termText.size();
    while(cnt<2000&&pos>0){pos=g_termText.rfind(L'\n',pos-1);if(pos==std::wstring::npos)break;cnt++;}
    if(cnt>=2000&&pos!=std::wstring::npos)g_termText=g_termText.substr(pos+1);
    InvalidateRect(g_hwnd,nullptr,FALSE);
}
void RunCmd(const std::wstring& cmd,const std::wstring& dir){
    SECURITY_ATTRIBUTES sa={sizeof(sa),nullptr,TRUE};
    HANDLE hr,hw; CreatePipe(&hr,&hw,&sa,0);
    SetHandleInformation(hr,HANDLE_FLAG_INHERIT,0);
    STARTUPINFO si={sizeof(si)};
    si.dwFlags=STARTF_USESTDHANDLES|STARTF_USESHOWWINDOW;
    si.hStdOutput=hw; si.hStdError=hw; si.wShowWindow=SW_HIDE;
    PROCESS_INFORMATION pi={};
    std::wstring c=cmd;
    if(!CreateProcess(nullptr,&c[0],nullptr,nullptr,TRUE,CREATE_NO_WINDOW,nullptr,
        dir.empty()?nullptr:dir.c_str(),&si,&pi)){
        AppendTerm(L"[ERROR] Cannot start: "+cmd+L"\r\n");
        CloseHandle(hw); CloseHandle(hr); return;
    }
    g_buildProc=pi.hProcess;
    CloseHandle(hw);
    char buf[4096]; DWORD rd;
    while(ReadFile(hr,buf,sizeof(buf)-1,&rd,nullptr)&&rd>0){
        buf[rd]=0;
        AppendTerm(S2W(std::string(buf,rd)));
    }
    DWORD code; GetExitCodeProcess(pi.hProcess,&code);
    AppendTerm(code==0?L"\r\n[+] Done.\r\n":L"\r\n[!] Failed.\r\n");
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread); CloseHandle(hr);
    g_buildProc=nullptr; g_building=false;
    InvalidateRect(g_hwnd,nullptr,FALSE);
}
void BuildAPK(bool release){
    if(g_building){AppendTerm(L"[!] Already building.\r\n");return;}
    if(g_projectPath.empty()){AppendTerm(L"[!] No project open.\r\n");return;}
    g_building=true;
    std::wstring type=release?L"Release":L"Debug";
    AppendTerm(L"\r\n══════════════════════════════════\r\n");
    AppendTerm(L"  Building "+type+L" APK...\r\n");
    AppendTerm(L"══════════════════════════════════\r\n");
    std::wstring cmd=g_projectPath+L"\\gradlew.bat "+(release?L"assembleRelease":L"assembleDebug")+L" --console=plain";
    std::thread([cmd](){RunCmd(cmd,g_projectPath);}).detach();
}
void InstallAPK(){
    if(g_projectPath.empty()){AppendTerm(L"[!] No project open.\r\n");return;}
    std::wstring rel=g_projectPath+L"\\app\\build\\outputs\\apk\\release\\app-release.apk";
    std::wstring dbg=g_projectPath+L"\\app\\build\\outputs\\apk\\debug\\app-debug.apk";
    std::wstring apk;
    if(GetFileAttributes(rel.c_str())!=INVALID_FILE_ATTRIBUTES){apk=rel;AppendTerm(L"[*] Release APK found.\r\n");}
    else if(GetFileAttributes(dbg.c_str())!=INVALID_FILE_ATTRIBUTES){apk=dbg;AppendTerm(L"[*] Debug APK found.\r\n");}
    else{AppendTerm(L"[ERROR] No APK found. Build first.\r\n");return;}
    std::wstring cmd=L"C:\\platform-tools\\adb.exe install -r \""+apk+L"\"";
    std::thread([cmd](){RunCmd(cmd,L"C:\\platform-tools");}).detach();
}
void StopBuild(){
    if(g_buildProc){TerminateProcess(g_buildProc,1);AppendTerm(L"\r\n[!] Stopped.\r\n");g_building=false;}
}
void CheckAdb(){
    g_adbBusy=true;
    SECURITY_ATTRIBUTES sa={sizeof(sa),nullptr,TRUE};
    HANDLE hr,hw; CreatePipe(&hr,&hw,&sa,0);
    SetHandleInformation(hr,HANDLE_FLAG_INHERIT,0);
    STARTUPINFO si={sizeof(si)};
    si.dwFlags=STARTF_USESTDHANDLES|STARTF_USESHOWWINDOW;
    si.hStdOutput=hw; si.hStdError=hw; si.wShowWindow=SW_HIDE;
    PROCESS_INFORMATION pi={};
    std::wstring cmd=L"C:\\platform-tools\\adb.exe devices";
    std::string out;
    if(CreateProcess(nullptr,&cmd[0],nullptr,nullptr,TRUE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi)){
        CloseHandle(hw);
        char buf[1024]; DWORD rd;
        while(ReadFile(hr,buf,sizeof(buf)-1,&rd,nullptr)&&rd>0){buf[rd]=0;out+=buf;}
        WaitForSingleObject(pi.hProcess,3000);
        CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
        int cnt=0;
        std::istringstream ss(out); std::string ln;
        while(std::getline(ss,ln))
            if(ln.find("device")!=std::string::npos&&ln.find("List")==std::string::npos)cnt++;
        if(cnt>0){g_adbStatus=std::to_wstring(cnt)+L" device(s)";g_adbCol=cGreen;}
        else{g_adbStatus=L"No device";g_adbCol=cRed;}
    }else{g_adbStatus=L"ADB not found";g_adbCol=cYellow;}
    CloseHandle(hr);
    g_adbBusy=false;
    InvalidateRect(g_hwnd,nullptr,FALSE);
}

// ════════════════════════════════════════
//  WNDPROC
// ════════════════════════════════════════
LRESULT CALLBACK WndProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
    switch(msg){
    case WM_CREATE:
        SetTimer(hwnd,1,5000,nullptr);
        return 0;
    case WM_TIMER:
        if(wp==1&&!g_adbBusy)std::thread([](){CheckAdb();}).detach();
        return 0;
    case WM_SIZE:{
        RECT rc; GetClientRect(hwnd,&rc);
        g_W=(int)(rc.right/g_scale); g_H=(int)(rc.bottom/g_scale);
        g_maximized=!!IsZoomed(hwnd);
        RecalcLayout(); InvalidateRect(hwnd,nullptr,FALSE);
        return 0;}
    case WM_PAINT:{
        PAINTSTRUCT ps; HDC hdc=BeginPaint(hwnd,&ps);
        DrawAll(hdc); EndPaint(hwnd,&ps);
        return 0;}
    case WM_ERASEBKGND: return 1;
    case WM_NCHITTEST:{
        POINT pt={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
        ScreenToClient(hwnd,&pt);
        float x=pt.x/g_scale,y=pt.y/g_scale,e=6;
        if(y<e&&x<e)return HTTOPLEFT;
        if(y<e&&x>g_W-e)return HTTOPRIGHT;
        if(y>g_H-e&&x<e)return HTBOTTOMLEFT;
        if(y>g_H-e&&x>g_W-e)return HTBOTTOMRIGHT;
        if(y<e)return HTTOP;
        if(y>g_H-e)return HTBOTTOM;
        if(x<e)return HTLEFT;
        if(x>g_W-e)return HTRIGHT;
        return HTCLIENT;}
    case WM_MOUSEMOVE:{
        float mx=GET_X_LPARAM(lp)/g_scale, my=GET_Y_LPARAM(lp)/g_scale;
        if(g_dragging){
            POINT cur; GetCursorPos(&cur);
            RECT wr; GetWindowRect(hwnd,&wr);
            SetWindowPos(hwnd,nullptr,wr.left+(cur.x-g_dragPt.x),wr.top+(cur.y-g_dragPt.y),0,0,SWP_NOSIZE|SWP_NOZORDER);
            g_dragPt=cur;
        }
        // split drag
        if(g_splitDrag){
            float newTermH=(rTerm.Y+rTerm.Height)-my;
            float totalH=rEditor.Height+rTerm.Height;
            TERMH_FRAC=newTermH/totalH;
            if(TERMH_FRAC<0.1f)TERMH_FRAC=0.1f;
            if(TERMH_FRAC>0.7f)TERMH_FRAC=0.7f;
            RecalcLayout();
        }
        // hover
        bool changed=false;
        auto chk=[&](BtnId id,RectF r){bool h=Hit(r,mx,my);if(g_btnHov[id]!=h){g_btnHov[id]=h;changed=true;}};
        chk(BID_CLOSE,rBtnClose);chk(BID_MAX,rBtnMax);chk(BID_MIN,rBtnMin);
        chk(BID_FOLDER,rBtnFolder);chk(BID_DEBUG,rBtnDebug);
        chk(BID_RELEASE,rBtnRelease);chk(BID_INSTALL,rBtnInstall);
        chk(BID_STOP,rBtnStop);chk(BID_ADB,rBtnAdb);
        // tree hover
        if(Hit(rSidebar,mx,my)){
            float y=rSidebar.Y+SBHW-(float)g_treeScroll,rh=20;
            g_treeHov=-1;
            for(int i=0;i<(int)g_flat.size();i++){
                if(my>=y&&my<y+rh){g_treeHov=i;changed=true;break;}
                y+=rh;
            }
        } else if(g_treeHov>=0){g_treeHov=-1;changed=true;}
        // cursor for split
        HCURSOR cur=LoadCursor(nullptr,Hit({rTerm.X,rTerm.Y-4,rTerm.Width,8},mx,my)?IDC_SIZENS:IDC_ARROW);
        SetCursor(cur);
        if(changed)InvalidateRect(hwnd,nullptr,FALSE);
        return 0;}
    case WM_LBUTTONDOWN:{
        SetCapture(hwnd);
        float mx=GET_X_LPARAM(lp)/g_scale, my=GET_Y_LPARAM(lp)/g_scale;
        // title buttons
        if(Hit(rBtnClose,mx,my)){PostMessage(hwnd,WM_CLOSE,0,0);return 0;}
        if(Hit(rBtnMax,mx,my)){g_maximized?ShowWindow(hwnd,SW_RESTORE):ShowWindow(hwnd,SW_MAXIMIZE);return 0;}
        if(Hit(rBtnMin,mx,my)){ShowWindow(hwnd,SW_MINIMIZE);return 0;}
        // title drag
        if(Hit(rTitle,mx,my)&&!g_maximized){g_dragging=true;GetCursorPos(&g_dragPt);return 0;}
        // toolbar
        if(Hit(rBtnFolder,mx,my)){BrowseFolder();return 0;}
        if(Hit(rBtnDebug,mx,my)&&!g_building&&!g_projectPath.empty()){BuildAPK(false);return 0;}
        if(Hit(rBtnRelease,mx,my)&&!g_building&&!g_projectPath.empty()){BuildAPK(true);return 0;}
        if(Hit(rBtnInstall,mx,my)&&!g_projectPath.empty()){InstallAPK();return 0;}
        if(Hit(rBtnStop,mx,my)&&g_building){StopBuild();return 0;}
        if(Hit(rBtnAdb,mx,my)&&!g_adbBusy){std::thread([](){CheckAdb();}).detach();return 0;}
        // split drag
        if(Hit({rTerm.X,rTerm.Y-5,rTerm.Width,10},mx,my)){g_splitDrag=true;return 0;}
        // file tree
        if(Hit(rSidebar,mx,my)){
            float y=rSidebar.Y+SBHW-(float)g_treeScroll,rh=20;
            for(int i=0;i<(int)g_flat.size();i++){
                if(my>=y&&my<y+rh){
                    g_treeSel=i;
                    FNode* n=g_flat[i];
                    if(n->isDir){ToggleExpand(*n);g_flat.clear();FlattenTree(g_tree,g_flat);}
                    else OpenTab(n->path);
                    break;
                }
                y+=rh;
            }
            InvalidateRect(hwnd,nullptr,FALSE);return 0;
        }
        // tabs
        if(Hit(rTabBar,mx,my)&&!g_tabs.empty()){
            float tx=rTabBar.X,tw=160;
            for(int i=0;i<(int)g_tabs.size();i++){
                // close btn
                RectF xr={tx+tw-20,rTabBar.Y+(rTabBar.Height-14)/2,14,14};
                if(Hit(xr,mx,my)){
                    g_tabs.erase(g_tabs.begin()+i);
                    if(g_activeTab>=(int)g_tabs.size())g_activeTab=(int)g_tabs.size()-1;
                    InvalidateRect(hwnd,nullptr,FALSE);return 0;
                }
                if(mx>=tx&&mx<tx+tw){g_activeTab=i;if(g_editCtrl)SetWindowText(g_editCtrl,g_tabs[i].content.c_str());InvalidateRect(hwnd,nullptr,FALSE);return 0;}
                tx+=tw;
            }
        }
        return 0;}
    case WM_LBUTTONUP:
        ReleaseCapture();g_dragging=false;g_splitDrag=false;
        return 0;
    case WM_LBUTTONDBLCLK:
        if(GET_Y_LPARAM(lp)/g_scale<TH){g_maximized?ShowWindow(hwnd,SW_RESTORE):ShowWindow(hwnd,SW_MAXIMIZE);}
        return 0;
    case WM_MOUSEWHEEL:{
        POINT pt={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
        ScreenToClient(hwnd,&pt);
        float mx=pt.x/g_scale,my=pt.y/g_scale;
        int d=GET_WHEEL_DELTA_WPARAM(wp)>0?-30:30;
        if(Hit(rSidebar,mx,my)){g_treeScroll+=d;if(g_treeScroll<0)g_treeScroll=0;}
        else if(Hit(rEditor,mx,my)){g_edScroll+=d;if(g_edScroll<0)g_edScroll=0;}
        InvalidateRect(hwnd,nullptr,FALSE);
        return 0;}
    case WM_CLOSE:
        if(g_building)StopBuild();
        DestroyWindow(hwnd);return 0;
    case WM_DESTROY:
        KillTimer(hwnd,1);PostQuitMessage(0);return 0;
    }
    return DefWindowProc(hwnd,msg,wp,lp);
}

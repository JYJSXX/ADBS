#include <cstdio>
#include <string>
#define FRAMESIZE 4096
#define DEFBUFSIZE 1024
#define MAXPAGES 50000
using namespace std;
struct bFrame {
  char field[FRAMESIZE];
};

struct BCB {
  BCB() {};
  BCB(int page_id, int frame_id) : page_id(page_id), frame_id(frame_id), count(0), latch(0), dirty(0), next(NULL) {};
  int page_id;
  int frame_id;
  int latch;
  int count;
  int dirty;
  BCB *next;
};

// LRU-2 节点结构
struct LRU2Node {
  int frame_id;
  int pincount;  // 访问次数
  LRU2Node *prev;
  LRU2Node *next;
  LRU2Node(int frame_id) : frame_id(frame_id), pincount(1), prev(NULL), next(NULL) {};
};

class DSMgr {
public:
  DSMgr();
  int OpenFile(string filename);
  int CloseFile();
  bFrame ReadPage(int page_id);
  int WritePage(int page_id, bFrame frm);
  int Seek(int offset, int pos);
  FILE *GetFile();
  void IncNumPages();
  int GetNumPages();
  void SetUse(int index, int use_bit);
  int GetUse(int index);
  int ReadCount;
  int WriteCount;

private:
  FILE *currFile;
  int numPages;
  int pages[MAXPAGES];
};

struct NewPage {
  int page_id;
  int frame_id;
};

class BMgr {
public:
  BMgr(int choice = 1); // choice: 1=LRU, 2=LRU-2
  ~BMgr();
  // Interface functions
  int FixPage(int page_id, int prot);
  NewPage FixNewPage();
  int UnfixPage(int page_id);
  int NumFreeFrames(); // Internal Functions
  int SelectVictim();
  BCB* Hash(int page_id);
  void RemoveBCB(BCB *ptr, int page_id);
  void RemoveLRUEle(int frid);
  void AddLRUEle(int frid);
  void SetDirty(int frame_id);
  void UnsetDirty(int frame_id);
  void WriteDirtys();
  void PrintFrame(int frame_id);
  int GetIONum(int choice);
  int InBufferCount;
  int OutBufferCount;

private: // Hash Table
  int ftop[DEFBUFSIZE];
  BCB *ptof[DEFBUFSIZE];
  int replace_policy; // 1=LRU, 2=LRU-2
  
  // LRU 双向链表：存储 frame_id 的顺序（头 = 最久未使用，尾 = 最近使用）
  int lru_prev[DEFBUFSIZE];
  int lru_next[DEFBUFSIZE];
  int lru_head;
  int lru_tail;
  
  // LRU-2 数据结构
  LRU2Node *Lhead, *Ltail;  // 历史记录链表（第一次访问）
  LRU2Node *L2head, *L2tail; // 缓存链表（第二次及以上访问）
  int Lsize, L2size;
  int Lmax;
  int L2max; // L2 最大长度，默认 800
  
  // LRU-2 辅助函数
  void LRU2Insert(int frame_id);
  void LRU2Fix(int frame_id);
  int LRU2Victim();
  void LRU2Remove();
  void LRU2Remove2();
  void LRU2Fix2(int frame_id);
  inline void set_pointer(LRU2Node* p, LRU2Node* q) {
    p->next = q;
    q->prev = p;
  }
};

// class DSManager {

// }
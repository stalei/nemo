// -*- C++ -*-                                                                 |
//-----------------------------------------------------------------------------+
//                                                                             |
/// \file src/public/tree.cc                                                   |
//                                                                             |
// Copyright (C) 2000-2005  Walter Dehnen                                      |
//                                                                             |
// This program is free software; you can redistribute it and/or modify        |
// it under the terms of the GNU General Public License as published by        |
// the Free Software Foundation; either version 2 of the License, or (at       |
// your option) any later version.                                             |
//                                                                             |
// This program is distributed in the hope that it will be useful, but         |
// WITHOUT ANY WARRANTY; without even the implied warranty of                  |
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU           |
// General Public License for more details.                                    |
//                                                                             |
// You should have received a copy of the GNU General Public License           |
// along with this program; if not, write to the Free Software                 |
// Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.                   |
//                                                                             |
//-----------------------------------------------------------------------------+
// #define TEST_TIMING
#include <public/tree.h>
#include <memory.h>
#include <body.h>

#ifdef  falcON_PROPER
#  define falcON_track_bug
//#  undef  falcON_track_bug
#endif
#ifdef falcON_track_bug
#  warning 
#  warning "adding bug-tracking code"
#  warning 
#endif

using namespace falcON;
////////////////////////////////////////////////////////////////////////////////
namespace falcON {
  //////////////////////////////////////////////////////////////////////////////
  //                                                                          //
  // class falcON::CellAccess                                                 //
  //                                                                          //
  // any class derived from this one has write access to the tree-specific    //
  // entries of tree cells, which are otherwise not writable.                 //
  //                                                                          //
  //////////////////////////////////////////////////////////////////////////////
  class CellAccess {
    //--------------------------------------------------------------------------
    // protected types and methods                                              
    //--------------------------------------------------------------------------
  protected:
    static uint8   &level_ (OctTree::Cell* const&C) { return C->LEVEL; }
    static uint8   &octant_(OctTree::Cell* const&C) { return C->OCTANT; }
#ifdef falcON_MPI
    static PeanoMap&peano_ (OctTree::Cell* const&C) { return C->PEANO; }
    static uint8   &key_   (OctTree::Cell* const&C) { return C->KEY; }
#endif
    static indx    &nleafs_(OctTree::Cell* const&C) { return C->NLEAFS; }
    static indx    &ncells_(OctTree::Cell* const&C) { return C->NCELLS; }
    static int     &number_(OctTree::Cell* const&C) { return C->NUMBER; }
    static int     &fcleaf_(OctTree::Cell* const&C) { return C->FCLEAF; }
    static int     &fccell_(OctTree::Cell* const&C) { return C->FCCELL; }
    static vect    &center_(OctTree::Cell* const&C) { return C->CENTER; }
    //--------------------------------------------------------------------------
    static void copy_sub  (OctTree::Cell* const&C, const OctTree::Cell* const&P) {
      C->copy_sub(P);
    }
    //--------------------------------------------------------------------------
    // public methods                                                           
    //--------------------------------------------------------------------------
  public:
    static size_t   NoLeaf (const OctTree      *T,
			    const OctTree::Leaf*L) {
      return T->NoLeaf(L);
    }
    static size_t   NoCell (const OctTree      *T,
			    const OctTree::Cell*C) {
      return T->NoCell(C);
    }
    static OctTree::Cell* const&FstCell(const OctTree*T) {
      return T->FstCell();
    }
    static OctTree::Leaf* const&FstLeaf(const OctTree*T) {
      return T->FstLeaf();
    }
    static OctTree::Cell*       EndCell(const OctTree*T) {
      return T->EndCell();
    }
    static OctTree::Leaf*       EndLeaf(const OctTree*T) {
      return T->EndLeaf();
    }
    static OctTree::Cell*       CellNo (const OctTree*T, int I) {
      return T->CellNo(I);
    }
    static OctTree::Leaf*       LeafNo (const OctTree*T, int I) {
      return T->LeafNo(I);
    }
  };
} // namespace falcON {
////////////////////////////////////////////////////////////////////////////////
namespace {
  using namespace falcON;
  using falcON::error;
  //////////////////////////////////////////////////////////////////////////////
  //                                                                          //
  // auxiliary macros                                                         //
  //                                                                          //
  //////////////////////////////////////////////////////////////////////////////
#define LoopDims for(int d=0; d!=Ndim; ++d)
  //----------------------------------------------------------------------------
#define __LoopLeafKids(TREE,CELL,KID)			\
    for(OctTree::Leaf* KID = LeafNo(TREE,fcleaf(CELL));	\
	KID != LeafNo(TREE,ecleaf(CELL)); ++KID)
#define __LoopAllLeafs(TREE,CELL,KID)			\
    for(OctTree::Leaf* KID = LeafNo(TREE,fcleaf(CELL));	\
	KID != LeafNo(TREE,ncleaf(CELL)); ++KID)
#define __LoopCellKids(TREE,CELL,KID)			\
    for(OctTree::Cell* KID = CellNo(TREE,fccell(CELL));	\
	KID != CellNo(TREE,eccell(CELL)); ++KID)
#define __LoopLeafs(TREE,LEAF)				\
    for(OctTree::Leaf* LEAF = FstLeaf(TREE);		\
        LEAF != EndLeaf(TREE); ++LEAF)
#define __LoopMyCellsUp(CELL)				\
    for(OctTree::Cell* CELL=EndCells()-1;		\
	CELL != FstCell()-1; --CELL)
#define __LoopLeafsRange(TREE,FIRST,END,LEAF)		\
    for(OctTree::Leaf* LEAF=LeafNo(TREE,FIRST);		\
	LEAF != LeafNo(TREE,END); ++LEAF)
  //////////////////////////////////////////////////////////////////////////////
  //                                                                          //
  // auxiliarty functions                                                     //
  //                                                                          //
  //////////////////////////////////////////////////////////////////////////////
  inline void flag_as_subtreecell(flags*F) {
    F->add   (flags::subtree_cell);
  }
  inline void unflag_subtree_flags(flags*F) { 
    F->un_set(flags::subtree_flags);
  }
  inline bool is_subtreecell(const flags*F)   {
    return F->is_set(flags::subtree_cell);
  }
  inline bool in_subtree(const OctTree::Leaf*L) {
    return in_subtree(flag(L));
  }
  //----------------------------------------------------------------------------
  // This routines returns, in each dimension, the nearest integer to x         
  //----------------------------------------------------------------------------
  inline vect Integer(const vect& x) {
    vect c(zero);                                  // reset return position     
    LoopDims c[d]=int(x[d]+half);                  // find center position      
    return c;                                      // and return it             
  }
  //----------------------------------------------------------------------------
  // in which octant of the cube centred on cen is pos?                         
  //----------------------------------------------------------------------------
  inline int octant(vect const&cen, vect const&pos) {
    int oct(pos[0] > cen[0]?        1 : 0);
    if     (pos[1] > cen[1]) oct |= 2;
    if     (pos[2] > cen[2]) oct |= 4;
    return oct;                                    // return octant             
  }
  //----------------------------------------------------------------------------
  // is pos inside the cube centred on cen and with radius (=half size) rad?    
  //----------------------------------------------------------------------------
  inline bool contains(const vect& cen,
		       const real& rad,
		       const vect& pos) {
    return abs(cen[0]-pos[0]) <= rad
      &&   abs(cen[1]-pos[1]) <= rad
      &&   abs(cen[2]-pos[2]) <= rad
      ;
  }
  //////////////////////////////////////////////////////////////////////////////
  //                                                                            
  // class falcON::SubTreeBuilder                                               
  //                                                                            
  //////////////////////////////////////////////////////////////////////////////
  class SubTreeBuilder : private CellAccess {
    //--------------------------------------------------------------------------
    // public static method                                                     
    //--------------------------------------------------------------------------
  public:
    static int link(const OctTree      *,          // I:   parent tree          
		    const OctTree::Cell*,          // I:   current parent cell  
		    const OctTree      *,          // I:   daughter tree        
		    OctTree::Cell      *,          // I:   current cell to link 
		    OctTree::Cell      *&,         // I/O: next free cell       
		    OctTree::Leaf      *&);        // I/O: next free leaf       
    //--------------------------------------------------------------------------
    static int link(                               // R:   depth of tree        
		    const OctTree*const&PT,        // I:   parent tree          
		    const OctTree*const&DT)        // I:   daughter tree        
    {
      OctTree::Leaf* Lf=FstLeaf(DT);
      OctTree::Cell* Cf=FstCell(DT)+1;
      return link(PT,FstCell(PT), DT, FstCell(DT), Cf, Lf);
    }
  };
  //----------------------------------------------------------------------------
  int 
  SubTreeBuilder::link(                            // R:   depth of tree        
		       const OctTree      * PT,    // I:   parent tree          
		       const OctTree::Cell* P,     // I:   current parent cell  
		       const OctTree      * T,     // I:   daughter tree        
		       OctTree::Cell      * C,     // I:   current cell to link 
		       OctTree::Cell      *&Cf,    // I/O: next free cell       
		       OctTree::Leaf      *&Lf)    // I/O: next free leaf       
  {
    int dep=0;                                     // depth                     
    copy_sub(C,P);                                 // copy level, octant, center
    nleafs_(C) = 0;                                // reset cell: # leaf kids   
    ncells_(C) = 0;                                // reset cell: # cell kids   
    fcleaf_(C) = NoLeaf(T,Lf);                     // set cell: sub-leafs       
    __LoopLeafKids(PT,P,pl)                        // LOOP(leaf kids of Pcell)  
      if(in_subtree(pl)) {                         //   IF(leaf == subt leaf)   
	(Lf++)->copy(pl);                          //     copy link to body etc 
	nleafs_(C)++;                              //     increment # leaf kids 
      }                                            //   ENDIF                   
    __LoopCellKids(PT,P,pc)                        // LOOP(cell kids of Pcell)  
      if(is_subtreecell(pc))                       //   IF(cell==subt cell)     
	ncells_(C)++;                              //     count # subt cell kids
      else if(in_subtree(pc))                      //   ELIF(cell==subt node)   
	__LoopAllLeafs(PT,pc,pl)                   //     LOOP sub cell's leafs 
	  if(in_subtree(pl)) {                     //       IF(leaf==subt leaf) 
	    (Lf++)->copy(pl);                      //         copy link etc     
	    nleafs_(C)++;                          //         incr # leaf kids  
	  }                                        //   ENDIF                   
    number_(C) = nleafs_(C);                       // # leafs >= # leaf kids    
    if(ncells_(C)) {                               // IF(cell has cell kids)    
      int de;                                      //   depth of sub-cell       
      OctTree::Cell*Ci=Cf;                         //   remember free cells     
      fccell_(C) = NoCell(T,Ci);                   //   set cell children       
      Cf += ncells_(C);                            //   reserve children cells  
      __LoopCellKids(PT,P,pc)                      //   LOOP(c kids of Pcell)   
	if(is_subtreecell(pc)) {                   //     IF(cell == subt cell) 
	  de =link(PT,pc,T,Ci,Cf,Lf);              //       link sub cells      
	  if(de>dep) dep=de;                       //       update depth        
	  number_(C)+= number_(Ci++);              //       count leaf descends 
	}                                          //     ENDIF                 
    } else                                         // ELSE                      
      fccell_(C) =-1;                              //   set pter to cell kids   
    return dep+1;                                  // return cells depth        
  }
  // ///////////////////////////////////////////////////////////////////////////
  //                                                                          //
  // class falcON::node                                                       //
  //                                                                          //
  /// base class for a box or a dot                                           //
  ///                                                                         //
  // ///////////////////////////////////////////////////////////////////////////
  class node {
  private:
    vect POS;                                      // position                  
    node(const node&);                             // not implemented           
  public:
    typedef node *node_pter;                       // pointer to node           
    node() {}                                      // default constructor       
    vect      &pos()       { return POS; }
    vect const&pos() const { return POS; }
  };
  //////////////////////////////////////////////////////////////////////////////
  //                                                                          //
  // class falcON::dot                                                        //
  //                                                                          //
  //////////////////////////////////////////////////////////////////////////////
  struct dot : public node {
    //--------------------------------------------------------------------------
    // data                                                                     
    //--------------------------------------------------------------------------
    mutable dot  *NEXT;
    bodies::index LINK;
    //--------------------------------------------------------------------------
    // methods                                                                  
    //--------------------------------------------------------------------------
    void add_to_list(dot* &LIST, int& COUNTER) {
      NEXT = LIST;
      LIST = this;
      ++COUNTER;
    }
    //--------------------------------------------------------------------------
    void add_to_list(node* &LIST, int& COUNTER) {
      NEXT = static_cast<dot*>(LIST);
      LIST = this;
      ++COUNTER;
    }
    //--------------------------------------------------------------------------
    void  set_up  (const OctTree::Leaf*L) {
      pos() = falcON::pos(L);
      LINK  = falcON::mybody(L);
    }
    //--------------------------------------------------------------------------
    void  set_up  (const bodies*B, bodies::index const&i) {
      LINK  = i;
      pos() = B->pos(i);
    }
    //--------------------------------------------------------------------------
    void  set_up  (body const&b) {
      LINK  = b;
      pos() = falcON::pos(b);
    }
    //--------------------------------------------------------------------------
    void  set_leaf(OctTree::Leaf*L) {
      L->set_link_and_pos(LINK,pos());
    }
  }; // class dot {
  //////////////////////////////////////////////////////////////////////////////
  //                                                                          //
  // class falcON::dot_list                                                   //
  //                                                                          //
  //////////////////////////////////////////////////////////////////////////////
  struct dot_list {
    dot *HEAD;                                     // head of list              
    int  SIZE;                                     // size of list              
    //--------------------------------------------------------------------------
    dot_list() : HEAD(0), SIZE(0) {}
    dot_list(dot* const&L, int const&N) : HEAD(L), SIZE(N) {}
  private:
    dot_list(const dot_list&L);                    // not implemented           
    dot_list& operator=(const dot_list&);          // not implemented           
    //--------------------------------------------------------------------------
  public:
    bool is_empty() { return SIZE == 0; }          // R: list empty?            
    //--------------------------------------------------------------------------
    void add_dot(dot*const&L) {                    // I: dot to be added        
      L->NEXT = HEAD;                              // set L' next to our list   
      HEAD    = L;                                 // update head of list       
      ++SIZE;                                      // increment size of list    
    }
    //--------------------------------------------------------------------------
    void append(dot_list const&L) {                // I: list to be appended    
      dot* T=L.HEAD;                               // take head of list L       
      while(T->NEXT) T=T->NEXT;                    // find tail of list L       
      T->NEXT = HEAD;                              // let it point to our list  
      HEAD    = L.HEAD;                            // update head of our list   
      SIZE   += L.SIZE;                            // update size               
    }
  }; // class dot_list
  //////////////////////////////////////////////////////////////////////////////
  // this macro requires you to close the curly bracket or use macro EndDotList 
#define BeginDotList(LIST,NAME)		           /* loop elements of list  */\
  for(register dot*NAME=LIST.HEAD, *NEXT_NAME;     /* pointers: current, next*/\
      NAME;					   /* loop until current=0   */\
      NAME = NEXT_NAME) { 			   /* set current = next     */\
  NEXT_NAME = NAME->NEXT;                          /* get next elemetnt      */
#define EndDotList }                               // close curly brackets      
  // this macro fails if dot.NEXT is manipulated within the loop                
#define LoopDotListS(LIST,NAME)		           /* loop elements of list  */\
  for(register dot* NAME = LIST.HEAD;	           /* current dot            */\
      NAME;					   /* loop until current=0   */\
      NAME = NAME->NEXT)                           // set current = next        
  //////////////////////////////////////////////////////////////////////////////
  //                                                                            
  // class falcON::box                                                          
  //                                                                            
  // basic link structure in a box-dot tree.                                    
  // a box represents a cube centered on center() with half size ("radius")     
  // equal to BoxDotTree::RA[LEVEL].                                            
  // if N <= Ncrit, it only contains sub-dots, which are in a linked list       
  // pointed to by DOTS.                                                        
  // if N >  Ncrit, DOTS=0 and the sub-nodes are in the array OCT of octants    
  //                                                                            
  //////////////////////////////////////////////////////////////////////////////
  struct box : public node {
    //--------------------------------------------------------------------------
    // data                                                                     
    //                                                                          
    // NOTE that if we make TYPE a char the code becomes significantly slower   
    //--------------------------------------------------------------------------
    indx     TYPE;                                 // bitfield: 1=cell, 0=dot   
    uint8    LEVEL;                                // tree level of box         
    PeanoMap PEANO;                                // Peano-Hilbert map within  
    node    *OCT[Nsub];                            // octants                   
    int      NUMBER;                               // number of dots            
    dot     *DOTS;                                 // linked list of dots       
    //--------------------------------------------------------------------------
    // const methods                                                            
    //--------------------------------------------------------------------------
    bool marked_as_box  (int const&i) const { return TYPE & (1<<i); }
    bool marked_as_dot  (int const&i) const { return !marked_as_box(i); }
    vect const&center   ()            const { return pos(); }
    //--------------------------------------------------------------------------
    /// octant of position \e x within box (not checked)
    int octant(const vect&x) const {
      return ::octant(pos(),x);
    }
    //--------------------------------------------------------------------------
    /// octant of dot within box (not checked)
    int octant(const dot*D) const {
      return ::octant(pos(),D->pos());
    }
    //--------------------------------------------------------------------------
    /// octant of box within this box (not checked)
    int octant(const box*P) const {
      return ::octant(pos(),P->pos());
    }
    //--------------------------------------------------------------------------
    /// octant of Cell within this box (not checked)
    int octant(const OctTree::Cell*C) const {
      return ::octant(pos(),falcON::center(C));
    }
    //--------------------------------------------------------------------------
    bool is_twig() const {
      return DOTS != 0;
    }
    //--------------------------------------------------------------------------
    // non-const methods                                                        
    //--------------------------------------------------------------------------
    box() {}
    //--------------------------------------------------------------------------
    void mark_as_box(int const&i) { TYPE |=  (1<<i); }
    vect&center     ()            { return pos(); }
    //--------------------------------------------------------------------------
    box& reset_octants() {
      for(register node**P=OCT; P!=OCT+Nsub; ++P) *P = 0;
      return *this;
    }
    //--------------------------------------------------------------------------
    box& reset() {
      TYPE     = 0;
      NUMBER   = 0;
      DOTS     = 0;
      reset_octants();
      return *this;
    }
    //--------------------------------------------------------------------------
    void adddot_to_list(dot* const&L) {            // add dot L to linked list  
      L->add_to_list(DOTS, NUMBER);                // add to linked list        
    }
    //--------------------------------------------------------------------------
    void adddot_to_octs(dot* const&L) {            // add dot L to octants      
      int b = octant(L);                           // find appropriate octant   
      OCT[b] = L;                                  // fill into octant          
      ++NUMBER;                                    // increment number          
    }
    //--------------------------------------------------------------------------
    void addbox_to_octs(box *const&P) {            // add box P to octants      
      int b = octant(P);                           // find appropriate octant   
      OCT[b] = P;                                  // fill into octant          
      mark_as_box(b);                              // mark octant as box        
      NUMBER += P->NUMBER;                         // increment number          
    }
    //--------------------------------------------------------------------------
    // const friends                                                            
    //--------------------------------------------------------------------------
    friend vect      &center (      box*const&B) {return B->center();  }
    friend vect const&center (const box*const&B) {return B->center();  }
  };// struct box {
} // namespace {
////////////////////////////////////////////////////////////////////////////////
falcON_TRAITS(::dot,"{tree.cc}::dot");
falcON_TRAITS(::dot_list,"{tree.cc}::dot_list");
falcON_TRAITS(::box,"{tree.cc}::box");
////////////////////////////////////////////////////////////////////////////////
namespace {
  //////////////////////////////////////////////////////////////////////////////
  //                                                                          //
  // class falcON::estimate_N_alloc                                           //
  //                                                                          //
  //////////////////////////////////////////////////////////////////////////////
  class estimate_N_alloc {
  private:
    const size_t &Ndots;
    const size_t &Nsofar;
  public:
    estimate_N_alloc(const size_t&a, const size_t&b) : Ndots(a), Nsofar(b) {}
    size_t operator() (const size_t&Nused) const {
      real x = Nused*(real(Ndots)/real(Nsofar)-one);
      return size_t(x+4*sqrt(x)+16);
    }
  };
  //////////////////////////////////////////////////////////////////////////////
  //                                                                            
  // class falcON::BoxDotTree                                                   
  //                                                                            
  // for building of a box-dot tree by adddot()                                 
  // for linking of the box-dot tree to a cell-leaf tree by link_cells()        
  // does not itself allocate the dots.                                         
  //                                                                            
  //////////////////////////////////////////////////////////////////////////////
  class BoxDotTree : protected CellAccess {
    BoxDotTree           (const BoxDotTree&);      // not implemented           
    BoxDotTree& operator=(const BoxDotTree&);      // not implemented           
    //--------------------------------------------------------------------------
    // data of class BoxDotTree                                                 
    //--------------------------------------------------------------------------
    int                NCRIT;                      // Ncrit                     
    int                DMAX, DEPTH;                // max/actual tree depth     
    size_t             NDOTS;                      // # dots (to be) added      
    block_alloc<box>  *BM;                         // allocator for boxes       
  protected:
    const OctTree     *TREE;                       // tree to link              
    real              *RA;                         // array with radius(level)  
    box               *P0;                         // root of box-dot tree      
#ifdef falcON_track_bug
    OctTree::Leaf     *LEND;                       // beyond leaf pter range    
    OctTree::Cell     *CEND;                       // beyond cell pter range    
#endif
    //--------------------------------------------------------------------------
    // protected methods are all inlined                                        
    //--------------------------------------------------------------------------
    // radius of box                                                            
    inline real const& radius(const box*B) const {
      return RA[B->LEVEL];
    }
    //--------------------------------------------------------------------------
    // does box contain a given position?                                       
    inline bool contains(const box*B, const vect&x) const {
      return ::contains(center(B),radius(B),x);
    }
    //--------------------------------------------------------------------------
    // does box contain a given dot?                                            
    inline bool contains(const box*B, const dot*D) const {
      return contains(B,D->pos());
    }
    //--------------------------------------------------------------------------
    // shrink box to its octant i                                               
    inline void shrink_to_octant(box*B, int i) {
      indx l = ++(B->LEVEL);
      if(l > DMAX)
	error("exceeding maximum tree depth of %d\n    "
	      "(presumably more than Ncrit=%d bodies have a common position "
	      "which may be NaN)", DMAX,NCRIT);
      real rad=RA[l];
      if(i&1) center(B)[0] += rad;  else  center(B)[0] -= rad;
      if(i&2) center(B)[1] += rad;  else  center(B)[1] -= rad;
      if(i&4) center(B)[2] += rad;  else  center(B)[2] -= rad;
    }
    //--------------------------------------------------------------------------
    inline box* new_box(size_t const&nl) {
      return &(BM->new_element(estimate_N_alloc(NDOTS,nl))->reset());
    }
    //--------------------------------------------------------------------------
    // provides a new empty (daughter) box in the i th octant of B              
    inline box* make_subbox(                       // R: new box                
			    const box*B,           // I: parent box             
			    int       i,           // I: parent box's octant    
			    size_t    nl)          // I: # dots added sofar     
    {
      box* P = new_box(nl);                        // get box off the stack     
      P->LEVEL    = B->LEVEL;                      // set level                 
      P->center() = B->center();                   // copy center of parent     
      shrink_to_octant(P,i);                       // shrink to correct octant  
#ifdef falcON_MPI
      P->PEANO    = B->PEANO;                      // copy peano map            
      P->PEANO.shift_to_kid(i);                    // shift peano map           
#endif
      return P;                                    // return new box            
    }
    //--------------------------------------------------------------------------
    // provides a new (daughter) box in the i th octant of B containing dot L   
    // requires that NCRIT == 1                                                 
    inline box* make_subbox_1(                     // R: new box                
			      const box*const&B,   // I: parent box             
			      int       const&i,   // I: parent box's octant    
			      dot      *const&L,   // I: dot of octant          
			      size_t    const&nl)  // I: # dots added sofar     
    {
      box* P = make_subbox(B,i,nl);                // make new sub-box          
      P->adddot_to_octs(L);                        // add dot to its octant     
      return P;                                    // return new box            
    }
    //--------------------------------------------------------------------------
    // provides a new (daughter) box in the i th octant of B containing dot L   
    // requires that NCRIT >  1                                                 
    inline box* make_subbox_N(                     // R: new box                
			      const box*const&B,   // I: parent box             
			      int       const&i,   // I: parent box's octant    
			      dot      *const&L,   // I: dot of octant          
			      size_t    const&nl)  // I: # dots added sofar     
    {
      box* P = make_subbox(B,i,nl);                // make new sub-box          
      P->adddot_to_list(L);                        // add old dot to list       
      return P;                                    // return new box            
    }
    //--------------------------------------------------------------------------
    // This routines splits a box:                                              
    // The dots in the linked list are sorted into octants. Octants with one    
    // dot will just hold that dot, octants with many dots will be boxes with   
    // the dots in the linked list.                                             
    // If all dots happen to be in just one octant, the process is repeated on  
    // the box of this octant.                                                  
    void split_box(box*         P,                 // I: box to be splitted     
		   size_t const&nl)                // I: # dots added so far    
    {
      int NUM[Nsub];                               // array with number of dots 
      int b,ne;                                    // octant & counters         
      box*sub=0;                                   // current sub-box           
      dot*Di,*Dn;                                  // current & next dot        
      do {                                         // DO until # octants > 1    
	for(b=0; b!=Nsub; ++b) NUM[b] = 0;         //   reset counters          
	for(Di=P->DOTS; Di; Di=Dn) {               //   LOOP linked list        
	  Dn = Di->NEXT;                           //     next dot in list      
	  b  = P->octant(Di);                      //     octant of current dot 
	  Di->add_to_list(P->OCT[b], NUM[b]);      //     add dot to list [b]   
	}                                          //   END LOOP                
	P->DOTS = 0;                               //   reset list of sub-dots  
	for(ne=b=0; b!=Nsub; ++b) if(NUM[b]) {     //   LOOP non-empty octs     
	  ne++;                                    //     count them            
	  if(NUM[b]>1) {                           //     IF many dots          
	    sub = make_subbox(P,b,nl);             //       make sub-box        
	    sub->DOTS = static_cast<dot*>(P->OCT[b]); //    assign sub-box's    
	    sub->NUMBER = NUM[b];                  //       dot list & number   
	    P->OCT[b] = sub;                       //       set octant=sub-box  
	    P->mark_as_box(b);                     //       mark octant as box  
	  }                                        //     ENDIF                 
	}                                          //   END LOOP                
	P = sub;                                   //   set current box=sub-box 
      } while(ne==1);                              // WHILE only 1 octant       
    }
    //--------------------------------------------------------------------------
    // This routine makes twig boxes contain at most 1 dot                      
    void adddot_1(                                 // add single dot to box     
		  box   *const&base,               // I: base box to add to     
		  dot   *const&Di,                 // I: dot to add             
		  size_t const&nl)                 // I: # dots added sofar     
    {
      for(box*P=base;;) {                          // LOOP over boxes           
	int    b  = P->octant(Di);                 //   dot's octant            
	node **oc = P->OCT+b;                      //   pointer to octant       
	P->NUMBER++;                               //   increment number        
	if((*oc)==0) {                             //   IF octant empty         
	  *oc = Di;                                //     assign dot to it      
	  return;                                  // <=  DONE with this dot    
	} else if(P->marked_as_dot(b)) {           //   ELIF octant=dot         
	  dot*Do = static_cast<dot*>(*oc);         //     get old dot           
	  P->mark_as_box(b);                       //     mark octant as box    
	  P = make_subbox_1(P,b,Do,nl);            //     create sub-box        
	  *oc = P;                                 //     assign sub-box to oc  
	} else                                     //   ELSE octant=box         
	  P = static_cast<box*>(*oc);              //     set current box       
      }                                            // END LOOP                  
    }
    //--------------------------------------------------------------------------
    // This routine makes twig boxes contain at most NCRIT > 1 dots             
    void adddot_N(                                 // add single dot to box     
		  box   *const&base,               // I: base box to add to     
		  dot   *const&Di,                 // I: dot to add             
		  size_t const&nl)                 // I: # dots added sofar     
    {
      for(box*P=base;;) {                          // LOOP over boxes           
	if(P->is_twig()) {                         //   IF box == twig          
	  P->adddot_to_list(Di);                   //     add dot to list       
	  if(P->NUMBER>NCRIT) split_box(P,nl);     //     IF(N > NCRIT) split   
	  return;                                  //     DONE with this dot    
	} else {                                   //   ELIF box==branch        
	  int    b  = P->octant(Di);               //     dot's octant          
	  node **oc = P->OCT+b;                    //     pointer to octant     
	  P->NUMBER++;                             //     increment number      
	  if((*oc)==0) {                           //     IF octant empty       
	    *oc = Di;                              //       assign dot to it    
	    return;                                // <-    DONE with this dot  
	  } else if(P->marked_as_dot(b)) {         //     ELIF octant=dot       
	    dot*Do = static_cast<dot*>(*oc);       //       get old dot         
	    P->mark_as_box(b);                     //       mark octant as box  
	    P = make_subbox_N(P,b,Do,nl);          //       create sub-box      
	    *oc = P;                               //       assign sub-box to oc
	  } else                                   //     ELSE octant=box       
	    P = static_cast<box*>(*oc);            //       set current box     
	}                                          //   ENDIF                   
      }                                            // END LOOP                  
    }
    //--------------------------------------------------------------------------
    // RECURSIVE                                                                
    // This routines transforms the box-dot tree into the cell-leaf tree,       
    // such that all the cells that are contained within some cell are conti-   
    // guous in memory, as are the leafs.                                       
    int link_cells_1(                              // R:   tree depth of cell   
		     const box*     ,              // I:   current box          
		     int            ,              // I:   octant of current box
		     int            ,              // I:   local peano key      
		     OctTree::Cell* ,              // I:   current cell         
		     OctTree::Cell*&,              // I/O: index: free cells    
		     OctTree::Leaf*&
#ifdef falcON_track_bug
		     ,
		     const dot *    ,
		     const dot *
#endif
		                     ) const;      // I/O: index: free leafs    
    //--------------------------------------------------------------------------
    // RECURSIVE                                                                
    // This routines transforms the box-dot tree into the cell-leaf tree,       
    // such that all the cells that are contained within some cell are conti-   
    // guous in memory, as are the leafs.                                       
    int link_cells_N(                              // R:   tree depth of cell   
		     const box*     ,              // I:   current box          
		     int            ,              // I:   octant of current box
		     int            ,              // I:   local peano key      
		     OctTree::Cell* ,              // I:   current cell         
		     OctTree::Cell*&,              // I/O: index: free cells    
		     OctTree::Leaf*&
#ifdef falcON_track_bug
		     ,
		     const dot *    ,
		     const dot *
#endif
		                     ) const;      // I/O: index: free leafs    
    //--------------------------------------------------------------------------
    BoxDotTree()
      : BM(0), TREE(0), RA(0), P0(0) {}
    //--------------------------------------------------------------------------
    // to be called before adding any dots.                                     
    // - allocates boxes                                                        
    // - initializes root cell                                                  
    void reset(const OctTree*t,                    // I: tree to be build       
	       int           nc,                   // I: N_crit                 
	       int           dm,                   // I: D_max                  
	       size_t        nl,                   // I: N_dots                 
	       vect    const&x0,                   // I: root center            
	       real          sz,                   // I: root radius            
	       size_t        nb = 0)               //[I: #boxes initially alloc]
    {
      NCRIT    = nc;
      DMAX     = dm;
      NDOTS    = nl;
      if(BM) falcON_DEL_O(BM);
      BM       = new block_alloc<box>(nb>0? nb : 1+NDOTS/4);
      TREE     = t;
      if(RA) falcON_DEL_A(RA);
      RA       = falcON_NEW(real,DMAX+1);
      P0       = new_box(1);
      RA[0]    = sz;
      for(int l=0; l!=DMAX; ++l) RA[l+1] = half * RA[l];
      P0->LEVEL = 0;
      P0->center() = x0;
#ifdef falcON_MPI
      P0->PEANO.set_root();
#endif
    }
    //--------------------------------------------------------------------------
    ~BoxDotTree()
    {
      if(BM) falcON_DEL_O(BM);
      if(RA) falcON_DEL_A(RA);
    }
    //--------------------------------------------------------------------------
    // const public methods (all inlined)                                       
    //--------------------------------------------------------------------------
  public:
    inline size_t       N_allocated() const { return BM->N_allocated(); }
    inline size_t       N_used     () const { return BM->N_used(); }
    inline size_t       N_boxes    () const { return BM->N_used(); }
    inline size_t       N_free     () const { return N_allocated()-N_used(); }
    inline int    const&depth      () const { return DEPTH; }
    inline int    const&maxdepth   () const { return DMAX; }
    inline int    const&Ncrit      () const { return NCRIT; }
    inline size_t const&N_dots     () const { return NDOTS; }
    inline int          N_levels   () const { return DMAX - P0->LEVEL; }
    inline box   *const&root       () const { return P0; }
    inline real   const&root_rad   () const { return RA[P0->LEVEL]; }
    //--------------------------------------------------------------------------
    // non-const public methods                                                 
    //--------------------------------------------------------------------------
#ifdef falcON_track_bug
#  define BUG_LINK_PARS ,D0,DN
#else
#  define BUG_LINK_PARS
#endif
    void link(
#ifdef falcON_track_bug
	      const dot*const&D0,
	      const dot*const&DN
#endif
	      )
    {
      report REPORT("BoxDotTree::link()");
#ifdef falcON_track_bug
      LEND = EndLeaf(TREE);
      if(LEND != LeafNo(TREE,N_dots()))
	error("BoxDotTree::link(): leaf number mismatch");
      CEND = EndCell(TREE);
      if(CEND != CellNo(TREE,N_boxes()))
	error("BoxDotTree::link(): cell number mismatch");
#endif
      OctTree::Cell* C0 = FstCell(TREE), *Cf=C0+1;
      OctTree::Leaf* Lf = FstLeaf(TREE);
      DEPTH = NCRIT > 1?
	link_cells_N(P0,0,0,C0,Cf,Lf BUG_LINK_PARS) :
	link_cells_1(P0,0,0,C0,Cf,Lf BUG_LINK_PARS) ;
    }
  };// class BoxDotTree
  //----------------------------------------------------------------------------
  int BoxDotTree::                                 // R:   tree depth of cell   
  link_cells_1(const box*     P,                   // I:   current box          
	       int            o,                   // I:   octant of current box
	       int            k,                   // I:   local peano key      
	       OctTree::Cell* C,                   // I:   current cell         
	       OctTree::Cell*&Cf,                  // I/O: index: free cells    
	       OctTree::Leaf*&Lf                   // I/O: index: free leafs    
#ifdef falcON_track_bug
	       ,
	       const dot*     D0,
	       const dot*     DN
#endif
	       ) const
  {
#ifdef falcON_track_bug
    if(C == CEND)
      report::info("TreeBuilder::link_cells_1(): >max # cells");
    if(!BM->is_element(P))
      report::info("TreeBuilder::link_cells_1(): invalid box*");
#endif
    int dep=0;                                     // depth of cell             
    level_ (C) = P->LEVEL;                         // copy level                
    octant_(C) = o;                                // set octant                
#ifdef falcON_MPI
    peano_ (C) = P->PEANO;                         // copy peano map            
    key_   (C) = k;                                // set local peano key       
#endif
    center_(C) = P->center();                      // copy center               
    number_(C) = P->NUMBER;                        // copy number               
    fcleaf_(C) = NoLeaf(TREE,Lf);                  // set cell: leaf kids       
    nleafs_(C) = 0;                                // reset cell: # leaf kids   
    int i,nsub=0;                                  // octant, counter: sub-boxes
    node*const*N;                                  // pter to sub-node          
    for(i=0, N=P->OCT; i!=Nsub; ++i,++N) if(*N)    // LOOP non-empty octants    
      if(P->marked_as_box(i)) ++nsub;              //   IF   sub-boxes: count   
      else {                                       //   ELIF sub-dots:          
#ifdef falcON_track_bug
	if(Lf == LEND)
	  report::info("TreeBuilder::link_cells_1(): >max # leafs");
	if(static_cast<dot*>(*N)<D0 || static_cast<dot*>(*N)>=DN)
	  report::info("TreeBuilder::link_cells_1(): invalid dot*");
#endif
	static_cast<dot*>(*N)->set_leaf(Lf++);     //     set leaf              
	nleafs_(C)++;                              //     inc # sub-leafs       
      }                                            // END LOOP                  
    if(nsub) {                                     // IF sub-boxes              
      OctTree::Cell*Ci=Cf;                         //   remember free cells     
      fccell_(C) = NoCell(TREE,Ci);                //   set cell: 1st sub-cell  
      ncells_(C) = nsub;                           //   set cell: # sub-cells   
      Cf += nsub;                                  //   reserve nsub cells      
      for(i=0, N=P->OCT; i!=Nsub; ++i,++N)         //   LOOP octants            
	if(*N && P->marked_as_box(i)) {            //     IF sub-box            
	  int de = link_cells_1(static_cast<box*>(*N), i,
#ifdef falcON_MPI
				P->PEANO.key(i),
#else
				0,
#endif
				Ci++, Cf, Lf BUG_LINK_PARS);
	  if(de>dep) dep=de;                       //       update depth        
	}                                          //   END LOOP                
    } else {                                       // ELSE (no sub-boxes)       
      fccell_(C) =-1;                              //   set cell: 1st sub-cell  
      ncells_(C) = 0;                              //   set cell: # sub-cells   
    }                                              // ENDIF                     
    dep++;                                         // increment depth           
    return dep;                                    // return cells depth        
  }
  //----------------------------------------------------------------------------
  // this routine appears to contain the code that results in a very rare and   
  // not reproducible Segmentation fault (possibly that is caused by an error   
  // elsewhere, i.e. the box-dot tree could be faulty).                         
  //----------------------------------------------------------------------------
  int BoxDotTree::                                 // R:   tree depth of cell   
  link_cells_N(const box*     P,                   // I:   current box          
	       int            o,                   // I:   octant of current box
	       int            k,                   // I:   local peano key      
	       OctTree::Cell* C,                   // I:   current cell         
	       OctTree::Cell*&Cf,                  // I/O: index: free cells    
	       OctTree::Leaf*&Lf                   // I/O: index: free leafs    
#ifdef falcON_track_bug
	       ,
	       const dot     *D0,
	       const dot     *DN
#endif
	       ) const
  {
#ifdef falcON_track_bug
    if(C == CEND)
      report::info("TreeBuilder::link_cells_N(): >max # cells");
    if(!BM->is_element(P))
      report::info("TreeBuilder::link_cells_N(): invalid box*");
#endif
    int dep=0;                                     // depth of cell             
    level_ (C) = P->LEVEL;                         // copy level                
    octant_(C) = o;                                // set octant                
#ifdef falcON_MPI
    peano_ (C) = P->PEANO;                         // copy peano map            
    key_   (C) = k;                                // set local peano key       
#endif
    center_(C) = P->center();                      // copy center               
    number_(C) = P->NUMBER;                        // copy number               
    fcleaf_(C) = NoLeaf(TREE,Lf);                  // set cell: leaf kids       
    if(P->is_twig()) {                             // IF box==twig              
      fccell_(C) =-1;                              //   set cell: sub-cells     
      ncells_(C) = 0;                              //   set cell: # cell kids   
      nleafs_(C) = P->NUMBER;                      //   set cell: # leaf kids   
      dot*Di=P->DOTS;                              //   sub-dot pointer         
      for(; Di; Di=Di->NEXT) {                     //   LOOP sub-dots           
#ifdef falcON_track_bug
	if(Lf == LEND)
	  report::info("TreeBuilder::link_cells_N(): >max # leafs in twig");
	if(Di<D0 || Di>=DN)
	  report::info("TreeBuilder::link_cells_N(): invalid dot* in twig");
#endif
	Di->set_leaf(Lf++);                        //     set leaf              
      }                                            //   END LOOP                
    } else {                                       // ELSE (box==branch)        
      nleafs_(C) = 0;                              //   reset cell: # leaf kids 
      int i,nsub=0;                                //   octant, # sub-boxes     
      node*const*N;                                //   sujb-node pointer       
      for(i=0,N=P->OCT; i!=Nsub; ++i,++N) if(*N)   //   LOOP non-empty octants  
	if(P->marked_as_box(i)) ++nsub;            //     IF   sub-boxes: count 
	else {                                     //     ELIF sub-dots:        
#ifdef falcON_track_bug
	  if(Lf == LEND)
	    report::info("TreeBuilder::link_cells_N(): >max # leafs");
	  if(static_cast<dot*>(*N)<D0 || static_cast<dot*>(*N)>=DN)
	    report::info("TreeBuilder::link_cells_N(): invalid dot*");
#endif
	  static_cast<dot*>(*N)->set_leaf(Lf++);   //       set leaf            
	  nleafs_(C)++;                            //       inc # sub-leafs     
      }                                            //   END LOOP                
      if(nsub) {                                   //   IF has sub-boxes        
	OctTree::Cell*Ci=Cf;                       //     remember free cells   
	fccell_(C) = NoCell(TREE,Ci);              //     set cell: 1st sub-cel 
	ncells_(C) = nsub;                         //     set cell: # cell kids 
	Cf += nsub;                                //     reserve nsub cells    
	for(i=0, N=P->OCT; i!=Nsub; ++i,++N)       //     LOOP octants          
	  if(*N && P->marked_as_box(i)) {          //       IF sub-box          
	    int de = link_cells_N(static_cast<box*>(*N), i,
#ifdef falcON_MPI
				  P->PEANO.key(i),
#else
				  0,
#endif
				  Ci++, Cf, Lf BUG_LINK_PARS);
	    if(de>dep) dep=de;                     //         update depth      
	  }                                        //     END LOOP              
      } else {                                     //   ELSE (no sub-boxes)     
	fccell_(C) =-1;                            //     set cell: 1st sub-cell
	ncells_(C) = 0;                            //     set cell: # sub-cells 
      }                                            //   ENDIF                   
    }                                              // ENDIF                     
    dep++;                                         // increment depth           
    return dep;                                    // return cell's depth       
  }
  //////////////////////////////////////////////////////////////////////////////
  //                                                                            
  // class falcON::TreeBuilder                                                  
  //                                                                            
  // for serial tree-building                                                   
  //                                                                            
  //////////////////////////////////////////////////////////////////////////////
  class TreeBuilder : public BoxDotTree {
    TreeBuilder           (const TreeBuilder&);    // not implemented           
    TreeBuilder& operator=(const TreeBuilder&);    // not implemented           
    //--------------------------------------------------------------------------
    // data of class TreeBuilder                                                
    //--------------------------------------------------------------------------
    const vect *ROOTCENTER;                        // pre-determined root center
    vect        XAVE, XMIN, XMAX;                  // extreme positions         
    dot        *D0, *DN;                           // begin/end of dots         
    //--------------------------------------------------------------------------
    // This routines returns the root center nearest to the mean position       
    inline vect root_center() {
      return ROOTCENTER? *ROOTCENTER : Integer(XAVE);
    }
    //--------------------------------------------------------------------------
    // This routines returns the half-size R of the smallest cube, centered     
    // on X that contains the points xmin and xmax.                             
    inline real root_radius(const vect& X) {
      real R,D=zero;                               // R, distance to dot        
      LoopDims {                                   // LOOP dimensions           
	R=max(abs(XMAX[d]-X[d]),abs(XMIN[d]-X[d]));//   distance to xmin, xmax  
	update_max(D,R);                           //   update maximum distance 
      }                                            // END LOOP                  
      return pow(two,int(one+log(D)/M_LN2));       // M_LN2 == log(2) (math.h)  
    }
    //--------------------------------------------------------------------------
    void setup_from_scratch(const bodies*,
			    flags       = flags::empty);
    //--------------------------------------------------------------------------
    void setup_from_scratch(const bodies*,
			    vect   const&,
			    vect   const&,
			    flags       = flags::empty);
    //--------------------------------------------------------------------------
    void setup_leaf_order  (const bodies*);
    //--------------------------------------------------------------------------
    // non-const public methods (almost all non-inline)                         
    //--------------------------------------------------------------------------
    // This routine simply calls adddots() with the root box as argument.       
  public:
    void build();                                  // build box-dot tree        
    //--------------------------------------------------------------------------
#ifdef falcON_track_bug
    void link() {
      BoxDotTree::link(D0,DN);
    }
#endif
    //--------------------------------------------------------------------------
    // constructors of class TreeBuilder                                        
    //--------------------------------------------------------------------------
    // 1   completely from scratch                                              
    //--------------------------------------------------------------------------
    TreeBuilder(const OctTree*,                    // I: tree to be build       
		const vect   *,                    // I: pre-determined center  
		int           ,                    // I: Ncrit                  
		int           ,                    // I: Dmax                   
		const bodies *,                    // I: body sources           
		flags        = flags::empty);      //[I: flag specifying bodies]
    //--------------------------------------------------------------------------
    TreeBuilder(const OctTree*,                    // I: tree to be build       
		const vect   *,                    // I: pre-determined center  
		int           ,                    // I: Ncrit                  
		int           ,                    // I: Dmax                   
		const bodies *,                    // I: body sources           
		vect    const&,                    // I: x_min                  
		vect    const&,                    // I: x_max                  
		flags        = flags::empty);      //[I: flag specifying bodies]
    //--------------------------------------------------------------------------
    // 2   from scratch, but aided by old tree                                  
    //     we put the dots to be added in the same order as the leafs of the    
    //     old tree. This reduces random memory access, yielding a significant  
    //     speed-up.                                                            
    //                                                                          
    // NOTE  In order to make the code more efficient, we no longer check for   
    //       any potential changes in the tree usage flags (in particular for   
    //       arrays). Thus, if those have changed, don't re-build the tree!     
    //--------------------------------------------------------------------------
    TreeBuilder(const OctTree*,                    // I: old/new tree           
		const vect   *,                    // I: pre-determined center  
		int           ,                    // I: Ncrit                  
		int           );                   // I: Dmax                   
    //--------------------------------------------------------------------------
    // destructor                                                               
    //--------------------------------------------------------------------------
    inline ~TreeBuilder()  {
      falcON_DEL_A(D0);
    }
    //--------------------------------------------------------------------------
  };
  //============================================================================
  // non-inline routines of class TreeBuilder<>                                 
  //============================================================================
  void TreeBuilder::build()
  {
    report REPORT("TreeBuilder::build()");
    size_t nl=0;                                   // counter: # dots added     
    dot   *Di;                                     // actual dot loaded         
    if(Ncrit() > 1)                                // IF(N_crit > 1)            
      for(Di=D0; Di!=DN; ++Di,++nl)                //   LOOP(dots)              
	adddot_N(P0,Di,nl);                        //     add dots              
    else                                           // ELSE                      
      for(Di=D0; Di!=DN; ++Di,++nl)                //   LOOP(dots)              
	adddot_1(P0,Di,nl);                        //     add dots              
  }
  //----------------------------------------------------------------------------
  void TreeBuilder::setup_from_scratch(const bodies*BB,
				       flags        SP)
  {
    D0 = falcON_NEW(dot,BB->N_bodies());           // allocate dots             
    dot* Di=D0;                                    // current dot               
    XAVE = zero;                                   // reset X_ave               
    if(SP && BB->have_flag()) {                    // IF take only some bodies  
      body b=BB->begin_all_bodies();               //   first body              
      XMAX = XMIN = pos(b);                        //   reset X_min/max         
      for(; b; ++b)                                //   LOOP bodies             
	if( flag(b).are_set(SP)) {                 //     IF body to be used    
	  Di->set_up(b);                           //       initialize dot      
	  if(isnan(Di->pos()))                     //       test for nan        
	    error("tree building: body position contains NaN\n");
	  Di->pos().up_min_max(XMIN,XMAX);
	  XAVE += Di->pos();                       //       sum up X            
	  Di++;                                    //       incr current dot    
	}                                          //   END LOOP                
    } else {                                       // ELSE use all bodies       
      body b=BB->begin_all_bodies();               //   first body              
      XMAX = XMIN = pos(b);                        //   reset X_min/max         
      for(; b; ++b) {                              //   LOOP bodies             
	Di->set_up(b);                             //     initialize dot        
	if(isnan(Di->pos()))                       //     test for nan          
	  error("tree building: body position contains NaN\n");
	Di->pos().up_min_max(XMIN,XMAX);
	XAVE += Di->pos();                         //     sum up X              
	Di++;                                      //     incr current dot      
      }                                            //   END LOOP                
    }                                              // ENDIF                     
    DN    = Di;                                    // set: beyond last dot      
    XAVE /= real(DN-D0);                           // set: X_ave                
  }
  //----------------------------------------------------------------------------
  void TreeBuilder::setup_from_scratch(const bodies*BB,
				       vect   const&xmin,
				       vect   const&xmax,
				       flags        SP)
  {
    D0 = falcON_NEW(dot,BB->N_bodies());           // allocate dots             
    dot* Di=D0;                                    // current dot               
    XAVE = zero;                                   // reset X_ave               
    XMIN = xmin;                                   // believe delivered x_min   
    XMAX = xmax;                                   // believe delivered x_max   
    if(SP && BB->have_flag()) {                    // IF take only some bodies  
      LoopAllBodies(BB,b)                          //   LOOP bodies             
	if( flag(b).are_set(SP)) {                 //     IF body to be used    
	  Di->set_up(b);                           //       initialize dot      
	  if(isnan(Di->pos()))                     //       test for nan        
	    error("tree building: body position contains nan\n");
	  XAVE += Di->pos();                       //       sum up X            
	  Di++;                                    //       incr current dot    
	}                                          //   END LOOP                
    } else {                                       // ELSE use all bodies       
      LoopAllBodies(BB,b) {                        //   LOOP bodies             
	Di->set_up(b);                             //     initialize dot        
	if(isnan(Di->pos()))                       //     test for nan          
	  error("tree building: body position contains nan\n");
	XAVE += Di->pos();                         //     sum up X              
	Di++;                                      //     incr current dot      
      }                                            //   END LOOP                
    }                                              // ENDIF                     
    DN    = Di;                                    // set: beyond last dot      
    XAVE /= real(DN-D0);                           // set: X_ave                
  }
  //----------------------------------------------------------------------------
  void TreeBuilder::setup_leaf_order(const bodies*BB)
  {
    D0 = falcON_NEW(dot,TREE->N_leafs());          // allocate dots             
    dot*Di = D0;                                   // current dot               
    XAVE = zero;                                   // reset X_ave               
    XMAX = XMIN = BB->pos(mybody(LeafNo(TREE,0))); // reset x_min & x_max       
    __LoopLeafs(TREE,Li) {                         // LOOP leaf                 
      Di->set_up(BB,mybody(Li));                   //   initialize dot          
      Di->pos().up_min_max(XMIN,XMAX);
      XAVE += Di->pos();                           //   sum up X                
      Di++;                                        //   incr current dot        
    }                                              // END LOOP                  
    DN    = Di;                                    // set: beyond last dot      
    XAVE /= real(DN-D0);                           // set: X_ave                
  }
  //----------------------------------------------------------------------------
  // constructor 1.1.1                                                          
  TreeBuilder::TreeBuilder(const OctTree*t,
			   const vect   *x0,
			   int           nc,
			   int           dm,
			   const bodies *bb,
			   flags         sp) :
    ROOTCENTER(x0)
  {
    report REPORT("TreeBuilder::TreeBuilder(): 1.1.1");
    setup_from_scratch(bb,sp);
    vect X0 = root_center();
    BoxDotTree::reset(t,nc,dm,size_t(DN-D0),X0,root_radius(X0));
  }
  //----------------------------------------------------------------------------
  // constructor 1.1.2                                                          
  TreeBuilder::TreeBuilder(const OctTree*t,
			   const vect   *x0,
			   int           nc,
			   int           dm,
			   const bodies *bb,
			   vect    const&xmin,
			   vect    const&xmax,
			   flags         sp) :
    ROOTCENTER(x0)
  {
    report REPORT("TreeBuilder::TreeBuilder(): 1.1.2");
    setup_from_scratch(bb,xmin,xmax,sp);
    vect X0 = root_center();
    BoxDotTree::reset(t,nc,dm,size_t(DN-D0),X0,root_radius(X0));
  }
  //----------------------------------------------------------------------------
  // constructor 2                                                              
  TreeBuilder::TreeBuilder(const OctTree*t,
			   const vect   *x0,
			   int           nc,
			   int           dm) : 
    ROOTCENTER(x0)
  {
    TREE = t;                                      // set tree                  
    report REPORT("TreeBuilder::TreeBuilder(): 2");
    setup_leaf_order(TREE->my_bodies());           // use leaf order            
    vect X0 = root_center();
    BoxDotTree::reset(t,nc,dm,size_t(DN-D0),X0,root_radius(X0));
  }
  //////////////////////////////////////////////////////////////////////////////
}                                                  // END: empty namespace      
////////////////////////////////////////////////////////////////////////////////
//                                                                            //
// class falcON::OctTree                                                      //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////
inline unsigned                                    // R: # subtree leafs        
OctTree::mark_sub(flags               F,           // I: subtree flag           
		  int                 Ncr,         // I: Ncrit                  
		  cell_iterator const&C,           // I: cell                   
		  unsigned           &nc) const    // O: # subtree cells        
  // RECURSIVE                                                                  
  // - count leafs in the subtree                                               
  // - flag cells with any subtree leafs as 'subtree'                           
  // - flag cells with more than Ncrit subtree leafs as 'subtree cells'         
{
  unflag_subtree_flags(C);                         // reset subtree flags       
  int ns=0;                                        // counter: subtree dots     
  LoopLeafKids(cell_iterator,C,Li)                 // LOOP leaf kids            
    if(are_set(Li,F)) {                            //   IF flag F is set        
      flag_for_subtree(Li);                        //     flag for subtree      
      ++ns;                                        //     count                 
    }                                              // END LOOP                  
  LoopCellKids(cell_iterator,C,Ci)                 // LOOP cell kids            
    ns += mark_sub(F,Ncr,Ci,nc);                   //   RECURSIVE call          
  if(ns) {                                         // IF any subtree leafs      
    flag_for_subtree(C);                           //   mark for subtree        
    if(ns >= Ncr) {                                //   IF >=Ncrit subree leafs 
      flag_as_subtreecell(C);                      //     mark as subtree cell  
      ++nc;                                        //     count subtree cells   
    }                                              //   ENDIF                   
  }                                                // ENDIF                     
  return ns;                                       // return: # subtree dots    
}
//------------------------------------------------------------------------------
void OctTree::mark_for_subtree(flags      F,       // I: flag for subtree       
			       int        Ncr,     // I: Ncrit for subtree      
			       unsigned  &Nsubc,   // O: # subtree cells        
			       unsigned  &Nsubs)   // O: # subtree leafs        
  const 
{
  if(Ncr > 1) {                                    // IF Ncrit > 1              
    Nsubc = 0u;                                    //   reset subt cell counter 
    Nsubs = mark_sub(F,Ncr,root(),Nsubc);          //   set flags: subtree_cell 
  } else {                                         // ELSE( Ncrit == 1)         
    unsigned subs=0,subc=0;                        //   counter: # subt nodes   
    LoopCellsUp(cell_iterator,this,Ci) {           //   LOOP cells up           
      unflag_subtree_flags(Ci);                    //     reset subtree flags   
      int ns=0;                                    //     # subt dots in cell   
      LoopLeafKids(cell_iterator,Ci,l)             //     LOOP child leafs      
	if(are_set(l,F)) {                         //       IF flag F is set    
	  flag_for_subtree(l);                     //         flag for subtree  
	  ++ns;                                    //         count             
	}                                          //     END LOOP              
      if(ns) {                                     //     IF any subt dots      
	subs += ns;                                //       count # subt dots   
	subc ++;                                   //       count # subt cells  
	flag_for_subtree(Ci);                      //       mark for subtree    
	flag_as_subtreecell(Ci);                   //       mark as subtree cell
      } else                                       //     ELSE (no subt dots)   
	LoopCellKids(cell_iterator,Ci,c)           //       LOOP child cells    
	  if(in_subtree(c)) {                      //         IF cell is in subt
	    flag_for_subtree(Ci);                  //           mark C: subtree 
	    flag_as_subtreecell(Ci);               //           mark C: subtcell
	    break;                                 //           break this loop 
	  }                                        //       END LOOP            
    }                                              //   END LOOP                
    Nsubc = subc;                                  //   set # subtree cells     
    Nsubs = subs;                                  //   set # subtree leafs     
  }                                                // ENDIF                     
}
//------------------------------------------------------------------------------
// construction and helpers                                                     
//------------------------------------------------------------------------------
inline void OctTree::allocate (unsigned ns, unsigned nc, unsigned dm, real r0) {
  const unsigned need =   4*sizeof(unsigned)       // Ns, Nc, Dp, Dm            
		        +ns*sizeof(Leaf)           // leafs                     
		        +nc*sizeof(Cell)           // cells                     
                        +(dm+1)*sizeof(real);      // radii of cells            
  if((need > NALLOC) || (need+need < NALLOC)) {
    if(ALLOC) delete16(ALLOC);
    ALLOC  = new16<char>(need);
    NALLOC = need;
  }
  DUINT[0] = Ns = ns;
  DUINT[1] = Nc = nc;
  DUINT[3] = dm;
  LEAFS    = static_cast<Leaf*>(
	     static_cast<void*>( DUINT+4 ));       // offset of 16bytes         
  CELLS    = static_cast<Cell*>(
             static_cast<void*>( LEAFS+Ns ));
  RA       = static_cast<real*>(
             static_cast<void*>( CELLS+Nc ));
  RA[0]    = r0;
  for(unsigned l=0; l!=dm; ++l) RA[l+1] = half * RA[l];
}
//------------------------------------------------------------------------------
inline void OctTree::set_depth(unsigned dp) {
  DUINT[2] = dp;
}
//------------------------------------------------------------------------------
// construction from bodies                                                     
//------------------------------------------------------------------------------
OctTree::OctTree(const bodies*bb,                  // I: body sources           
		 int          nc,                  // I: N_crit                 
		 const vect  *x0,                  // I: pre-determined center  
		 int          dm,                  // I: max tree depth         
		 flags        sp) :                // I: flag specifying bodies 
  BSRCES(bb), SPFLAG(sp), LEAFS(0), CELLS(0), ALLOC(0), NALLOC(0u),
  STATE(fresh), USAGE(un_used)
{
  SET_I
  TreeBuilder TB(this,x0,nc,dm,bb,sp);             // initialize TreeBuilder    
  SET_T(" time for TreeBuilder::TreeBuilder(): ");
  if(TB.N_dots()) {                                // IF(dots in tree)          
    TB.build();                                    //   build box-dot tree      
    SET_T(" time for TreeBuilder::build():        ");
    allocate(TB.N_dots(),TB.N_boxes(),             //   allocate leafs & cells  
	     TB.N_levels(),TB.root_rad());         //   & set up table: radii   
    TB.link();                                     //   box-dot -> cell-leaf    
    set_depth(TB.depth());                         //   set tree depth          
    SET_T(" time for TreeBuilder::link():         ");
  } else {                                         // ELSE                      
    warning("nobody in tree");                     //   issue a warning         
    allocate(0,0,0,zero);                          //   reset leafs & cells     
    set_depth(0);                                  //   set tree depth to zero  
  }                                                // ENDIF                     
  RCENTER = center(root());                        // set root center           
}  
//------------------------------------------------------------------------------
// construction from bodies with X_min/max known already                        
//------------------------------------------------------------------------------
OctTree::OctTree(const bodies*bb,            // I: body sources           
		 vect   const&xi,            // I: x_min                  
		 vect   const&xa,            // I: x_max                  
		 int          nc,            // I: N_crit                 
		 const vect  *x0,            // I: pre-determined center  
		 int          dm,            // I: max tree depth         
		 flags        sp) :          // I: flag specifying bodies 
  BSRCES(bb), SPFLAG(sp), LEAFS(0), CELLS(0), ALLOC(0), NALLOC(0u),
  STATE(fresh), USAGE(un_used)
{
  SET_I
  if(dm >= 1<<8)
    error("OctTree: maximum tree depth must not exceed %d",1<<8-1);
  TreeBuilder TB(this,x0,nc,dm,bb,xi,xa,sp);      // initialize TreeBuilder   
  SET_T(" time for TreeBuilder::TreeBuilder(): ");
  if(TB.N_dots()) {                                // IF(dots in tree)          
    TB.build();                                    //   build box-dot tree      
    SET_T(" time for TreeBuilder::build():        ");
    allocate(TB.N_dots(),TB.N_boxes(),             //   allocate leafs & cells  
	     TB.N_levels(),TB.root_rad());         //   & set up table: radii   
    TB.link();                                     //   box-dot -> cell-leaf    
    set_depth(TB.depth());                         //   set tree depth          
    SET_T(" time for TreeBuilder::link():         ");
  } else {                                         // ELSE                      
    warning("nobody in tree");                     //   issue a warning         
    allocate(0,0,0,zero);                          //   reset leafs & cells     
    set_depth(0);                                  //   set tree depth to zero  
  }                                                // ENDIF                     
  RCENTER = center(root());                        // set root center           
}
//------------------------------------------------------------------------------
// construction as sub-tree from another tree                                   
//------------------------------------------------------------------------------
OctTree::OctTree(const OctTree*par,                // I: parent tree            
		 flags         F,                  // I: flag specif'ing subtree
		 int           Ncrit) :            //[I: N_crit]                
  BSRCES(par->my_bodies()),                        // copy parent's  bodies     
  SPFLAG(par->SP_flag() | F),                      // copy body specific flag   
  LEAFS(0), CELLS(0), ALLOC(0), NALLOC(0u),        // reset some data           
  STATE ( state( par->STATE | sub_tree) ),         // set state                 
  USAGE ( un_used )                                // set usage                 
{
  par->mark_for_subtree(F,Ncrit,Nc,Ns);            // mark parent tree          
  if(Ns==0 || Nc==0) {                             // IF no nodes marked        
    warning("empty subtree");                      //   issue warning and       
    allocate(0,0,0,zero);                          //   reset leafs & cells     
    set_depth(0);                                  //   set tree depth to zero  
  } else {                                         // ELSE                      
    allocate(Ns,Nc,par->depth(),
	     par->root_radius());                  //   allocate leafs & cells  
    set_depth(                                     //   set tree depth          
	      SubTreeBuilder::link(par,this));     //   link sub-tree           
  }                                                // ENDIF                     
  RCENTER = center(root());                        // set root center           
}
//------------------------------------------------------------------------------
// building using the leaf-order of the old tree structure                      
//------------------------------------------------------------------------------
void OctTree::build(int        const&nc,           //[I: N_crit]                
		    const vect*const&x0,           //[I: pre-determined center] 
		    int        const&dm)           //[I: max tree depth]        
{
  report REPORT("OctTree::build(%d,%d)",nc,dm);
  SET_I
  if(dm >= 1<<8)
    error("OctTree: maximum tree depth must not exceed %d",1<<8-1);
  TreeBuilder TB(this,x0,nc,dm);                   // initialize TreeBuilder    
  SET_T(" time for TreeBuilder::TreeBuilder(): ");
  if(TB.N_dots()) {                                // IF(dots in tree)          
    TB.build();                                    //   build box-dot tree      
    SET_T(" time for TreeBuilder::build():        ");
    allocate(TB.N_dots(),TB.N_boxes(),             //   allocate leafs & cells  
	     TB.N_levels(),TB.root_rad());         //   & set up table: radii   
    TB.link();                                     //   box-dot -> cell-leaf    
    set_depth(TB.depth());                         //   set tree depth          
    SET_T(" time for TreeBuilder::link():         ");
  } else {                                         // ELSE                      
    warning("nobody in tree");                     //   issue a warning         
    allocate(0,0,0,zero);                          //   reset leafs & cells     
    set_depth(0);                                  //   set tree depth to zero  
  }                                                // ENDIF                     
  STATE = state((STATE & origins) | re_grown);     // reset state               
  USAGE = un_used;                                 // reset usage flag          
  RCENTER = center(root());                        // set root center           
}
//------------------------------------------------------------------------------
// re-using old tree structure                                                  
//------------------------------------------------------------------------------
void OctTree::reuse() {
  for(leaf_iterator Li=begin_leafs(); Li!=end_leafs(); ++Li)
    Li->copy_from_bodies_pos(BSRCES);
  STATE = state((STATE & origins) | re_used);      // reset state               
  USAGE = un_used;                                 // reset usage flag          
}
////////////////////////////////////////////////////////////////////////////////
OctTree::~OctTree()
{
  if(ALLOC) { delete16(ALLOC); }
}
////////////////////////////////////////////////////////////////////////////////
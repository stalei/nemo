// -*- C++ -*-                                                                  
////////////////////////////////////////////////////////////////////////////////
///                                                                             
/// \file   src/mains/s2a.cc                                                    
///                                                                             
/// \author Walter Dehnen                                                       
/// \date   2002-2006                                                           
///                                                                             
////////////////////////////////////////////////////////////////////////////////
//                                                                              
// Copyright (C) 2002-2006 Walter Dehnen                                        
//                                                                              
// This program is free software; you can redistribute it and/or modify         
// it under the terms of the GNU General Public License as published by         
// the Free Software Foundation; either version 2 of the License, or (at        
// your option) any later version.                                              
//                                                                              
// This program is distributed in the hope that it will be useful, but          
// WITHOUT ANY WARRANTY; without even the implied warranty of                   
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU            
// General Public License for more details.                                     
//                                                                              
// You should have received a copy of the GNU General Public License            
// along with this program; if not, write to the Free Software                  
// Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.                    
//                                                                              
////////////////////////////////////////////////////////////////////////////////
//                                                                              
// history:                                                                     
//                                                                              
// v 0.0   29/10/2003  WD created.                                              
// v 0.1   10/02/2004  WD debugged (body.cc: SPH)                               
// v 1.0   10/02/2004  WD automized output; report history, run info            
// v 1.1   30/04/2004  WD improved automisation; new body.h; happy icc 8.0      
// v 2.0   20/09/2004  WD write defaults to all present data; snapshot;         
// v 2.1   24/10/2004  WD parameter header                                      
// v 2.2   11/11/2004  WD format string in out; index=                          
// v 2.3   20/05/2005  WD several minor updates                                 
// v 3.0   24/06/2004  WD new falcON                                            
// v 3.1   27/06/2004  WD deBUGged                                              
// v 4.0   15/11/2005  WD added filter (proprietary only)                       
// v 4.0.1 16/11/2005  WD improvements in filter (BodyFunc<>)                   
// v 4.1   04/07/2006  WD made public (along with bodyfunc.h)                   
////////////////////////////////////////////////////////////////////////////////
#define falcON_VERSION   "4.1"
#define falcON_VERSION_D "04-jul-2006 Walter Dehnen                          "
//-----------------------------------------------------------------------------+
#ifndef falcON_NEMO                                // this is a NEMO program    
#  error You need NEMO to compile "s2a"
#endif
#define falcON_RepAction 0                         // no action reporting       
//-----------------------------------------------------------------------------+
#include <body.h>                                  // bodies etc..              
#include <public/io.h>                             // WDs C++ NEMO I/O          
#ifdef falcON_PROPER
#  include <public/bodyfunc.h>                     // body functions            
#endif
#include <main.h>                                  // NEMO basics & main        
#include <cstdio>                                  // C std I/O                 
//------------------------------------------------------------------------------
string defv[] = {
  "in=???\n         input snapshot file                                ",
  "out=-\n          file for ascii output (may contain format string)  ",
  "times=all\n      times to process                                   ",
  "write=\n         select data to write out (default: all)            ",
#ifdef falcON_PROPER
  "filter=\n        bodyfunc filter (boolean): which bodies to write   ",
  "pars=\n          parameters for filter, if any                      ",
#endif
  "iformat=%d\n     format for integers                                ",
  "rformat=%14.6E\n format for real numbers                            ",
  "index=\n         first index if out contains format string          ",
  "header=t\n       write header                                       ",
  falcON_DEFV, NULL };
//------------------------------------------------------------------------------
string usage = "s2a -- Walter's simple snapshot to ascii converter\n";
//------------------------------------------------------------------------------
namespace {
  //----------------------------------------------------------------------------
  falcON::fieldset OUTPUT = falcON::fieldset::o;
  FILE*            OUT       = 0;
  char             RFORMAT[32], IFORMAT[32], VFORMAT[96];
  //----------------------------------------------------------------------------
  template<typename T> struct Print {
    static void print(T const&t) {
      fprintf(OUT,IFORMAT,(int)(t));
    } };
  //----------------------------------------------------------------------------
  template<> struct Print<falcON::real> {
    static void print(falcON::real const&t) {
      fprintf(OUT,RFORMAT,t);
    } };
  //----------------------------------------------------------------------------
  template<> struct Print<falcON::vect> {
    static void print(falcON::vect const&t) {
      fprintf(OUT,VFORMAT,t[0],t[1],t[2]);
    } };
  //----------------------------------------------------------------------------
  template<typename T> 
  void print(T const&t) { Print<T>::print(t); }
  //----------------------------------------------------------------------------
  template<int BIT> struct BodyPrint    {
    static void act(falcON::body const&b) {
      if(OUTPUT.contain(falcON::fieldbit(BIT)))
	print(falcON::const_datum<BIT>(b));
    }
  };
  //----------------------------------------------------------------------------
}
//------------------------------------------------------------------------------
void falcON::main() falcON_THROWING {
  nemo_in        IN   (getparam("in"));
  const fieldset NEED (getioparam_a("write"));
  fieldset       READ;
  snapshot       SHOT;
  int            INDEX(getiparam_z("index"));
  const bool     FOUT (hasvalue("out") && strcmp(getparam("out"),"-"));
  strcpy(IFORMAT,getparam("iformat")); strcat(IFORMAT," ");
  strcpy(RFORMAT,getparam("rformat")); strcat(RFORMAT," ");
  strcpy(VFORMAT,RFORMAT);
  strcat(VFORMAT,RFORMAT);
  strcat(VFORMAT,RFORMAT);
#ifdef falcON_PROPER
  BodyFunc<bool>*F=
    hasvalue("filter")? new BodyFunc<bool>(getparam("filter"),
					   getparam_z("pars")) : 0;
#endif
  if(!FOUT) OUT = stdout;
  while(IN.has_snapshot()) {
    if(! SHOT.read_nemo(IN,READ,NEED,getparam("times"),0)) continue;
    unsigned Nb[BT_NUM]={0u}, Ntot=0u;
#ifdef falcON_PROPER
    if(F) {
      SHOT.add_field(fieldbit::f);
      for(bodytype t; t; ++t) {
	LoopTypedBodies(&SHOT,b,t)
	  if( (*F)(b,SHOT.time()) ) {
	    b.mark();
	    ++(Nb[t]);
	  }
	Ntot += Nb[t];
      }
    } else
#endif
      for(bodytype t; t; ++t) {
	Nb[t] = SHOT.N_bodies(t);
	Ntot += Nb[t];
      }
    if(FOUT) {
      char FNAME[256];
      sprintf(FNAME,getparam("out"),INDEX++);
      if(strcmp(getparam("out"), FNAME)) {
	if(OUT) fclose(OUT);
	OUT = fopen(FNAME,"w");
      } else if(OUT==0)
	OUT = fopen(FNAME,"w");
    }
    OUTPUT = READ & NEED;
    if(getbparam("header")) {
      fprintf(OUT,"#\n# %s\n#\n", *(ask_history()));
      fprintf(OUT,"# run %s\n",RunInfo::time());
      if(RunInfo::user_known())
	fprintf(OUT,"#  by user  %s\n",RunInfo::user());
      if(RunInfo::host_known())
	fprintf(OUT,"#  on host  %s\n",RunInfo::host());
      if(RunInfo::pid_known())
	fprintf(OUT,"#  with pid %s\n",RunInfo::pid());
      fprintf(OUT,"#\n# time: %f\n# Nbod: %d\n# Ngas: %d\n#\n#",
	      SHOT.time(),Ntot,Nb[bodytype::gas]);
      for(fieldbit f; f; ++f)
	if(OUTPUT.contain(f) && SHOT.have(f))
	  fprintf(OUT," %s",name(f));
      fprintf(OUT,"\n#\n");
    }
#ifdef falcON_PROPER
    if(F) {
      LoopSPHBodies(&SHOT,b)
	if( is_marked(b) ) {
	  LoopAllFields<BodyPrint>::const_loop(b);
	  fprintf(OUT,"\n");
	}
      LoopSTDBodies(&SHOT,b)
	if( is_marked(b) ) {
	  LoopSTDFields<BodyPrint>::const_loop(b);
	  fprintf(OUT,"\n");
	}
    } else
#endif
    {
      LoopSPHBodies(&SHOT,b) {
	LoopAllFields<BodyPrint>::const_loop(b);
	fprintf(OUT,"\n");
      }
      LoopSTDBodies(&SHOT,b) {
	LoopSTDFields<BodyPrint>::const_loop(b);
	fprintf(OUT,"\n");
      }
    }
  }
  if(FOUT) fclose(OUT);
#ifdef falcON_PROPER
  if(F) falcON_DEL_O(F);
#endif
}
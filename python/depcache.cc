// -*- mode: cpp; mode: fold -*-
// Description								/*{{{*/
// $Id: depcache.cc,v 1.5 2003/06/03 03:03:23 mdz Exp $
/* ######################################################################

   DepCache - Wrapper for the depcache related functions

   ##################################################################### */
									/*}}}*/
// Include Files							/*{{{*/
#include "generic.h"
#include "apt_pkgmodule.h"

#include <apt-pkg/pkgcache.h>
#include <apt-pkg/cachefile.h>
#include <apt-pkg/algorithms.h>
#include <apt-pkg/policy.h>
#include <apt-pkg/sptr.h>
#include <apt-pkg/configuration.h>
#include <apt-pkg/error.h>
#include <apt-pkg/pkgsystem.h>
#include <apt-pkg/sourcelist.h>
#include <apt-pkg/pkgrecords.h>
#include <apt-pkg/acquire-item.h>
#include <Python.h>

#include <iostream>
#include "progress.h"

// DepCache Class								/*{{{*/
// ---------------------------------------------------------------------

struct PkgDepCacheStruct
{
   pkgDepCache *depcache;
   pkgPolicy *policy;

   PkgDepCacheStruct(pkgCache *Cache) {
      policy = new pkgPolicy(Cache);
      depcache = new pkgDepCache(Cache, policy);
   }
   virtual ~PkgDepCacheStruct() {
      delete depcache;
      delete policy;
   };


   PkgDepCacheStruct() {abort();};
};



static PyObject *PkgDepCacheInit(PyObject *Self,PyObject *Args)
{   
   PkgDepCacheStruct &Struct = GetCpp<PkgDepCacheStruct>(Self);

   PyObject *pyCallbackInst = 0;
   if (PyArg_ParseTuple(Args, "|O", &pyCallbackInst) == 0)
      return 0;

   if(pyCallbackInst != 0) {
      PyOpProgress progress;
      progress.setCallbackInst(pyCallbackInst);
      Struct.depcache->Init(&progress);
   } else {
      Struct.depcache->Init(0);
   }

   Py_INCREF(Py_None);
   return HandleErrors(Py_None);   
}

static PyObject *PkgDepCacheCommit(PyObject *Self,PyObject *Args)
{   
   PkgDepCacheStruct &Struct = GetCpp<PkgDepCacheStruct>(Self);

   PyObject *pyInstallProgressInst = 0;
   PyObject *pyFetchProgressInst = 0;
   if (PyArg_ParseTuple(Args, "OO", 
			&pyFetchProgressInst, &pyInstallProgressInst) == 0) {
      return 0;
   }
   FileFd Lock;
   if (_config->FindB("Debug::NoLocking", false) == false) {
      Lock.Fd(GetLock(_config->FindDir("Dir::Cache::Archives") + "lock"));
      if (_error->PendingError() == true)
         return HandleErrors();
   }
   
   pkgRecords Recs(*Struct.depcache);
   if (_error->PendingError() == true)
      HandleErrors(Py_None);

   pkgSourceList List;
   if(!List.ReadMainList())
      return HandleErrors(Py_None);

   PyFetchProgress progress;
   progress.setCallbackInst(pyFetchProgressInst);

   pkgAcquire Fetcher(&progress);
   pkgPackageManager *PM;
   PM = _system->CreatePM(Struct.depcache);
   if(PM->GetArchives(&Fetcher, &List, &Recs) == false ||
      _error->PendingError() == true) {
      std::cerr << "Error in GetArchives" << std::endl;
      return HandleErrors();
   }

   std::cout << "PM created" << std::endl;

   // Run it
   while (1)
   {
      bool Transient = false;
      
      if (Fetcher.Run() == pkgAcquire::Failed)
	 return false;
      
      // Print out errors
      bool Failed = false;
      for (pkgAcquire::ItemIterator I = Fetcher.ItemsBegin(); I != Fetcher.ItemsEnd(); I++)
      {
	 if ((*I)->Status == pkgAcquire::Item::StatDone &&
	     (*I)->Complete == true)
	    continue;
	 
	 if ((*I)->Status == pkgAcquire::Item::StatIdle)
	 {
	    Transient = true;
	    // Failed = true;
	    continue;
	 }

	 //FIXME: report this error somehow
// 	 fprintf(stderr,_("Failed to fetch %s  %s\n"),(*I)->DescURI().c_str(),
// 		 (*I)->ErrorText.c_str());
	 Failed = true;
      }

#if 0 // check that stuff
      if (Transient == true && Failed == true)
	 return Py_None; /*_error->Error(_("--fix-missing and media swapping is not currently supported"));*/
      
      // Try to deal with missing package files
      if (Failed == true && PM->FixMissing() == false)
      {
	 //std::cerr << "Unable to correct missing packages." << std::endl;
	 _error->Error("Aborting install.");
	 return HandleErrors(Py_None);
      }
#endif       	 

      _system->UnLock();
      pkgPackageManager::OrderResult Res = PM->DoInstall();
      if (Res == pkgPackageManager::Failed || _error->PendingError() == true)
	 return Py_None/*false;*/;
      if (Res == pkgPackageManager::Completed)
	 return Py_None /*true;*/;
      
      // Reload the fetcher object and loop again for media swapping
      Fetcher.Shutdown();
      if (PM->GetArchives(&Fetcher,&List,&Recs) == false)
	 return Py_None /*false;*/;
      
      _system->Lock();
   }   



#if 0
   if (Fetcher.Run() == pkgAcquire::Failed)
      return HandleErrors(Py_None);

   std::cout << "Fetcher was run" << std::endl;

   // FIXME: incomplete, see apt-get.cc
   _system->UnLock();

   pkgPackageManager::OrderResult Res = PM->DoInstall();
   if (Res == pkgPackageManager::Failed || _error->PendingError() == true)
      return Py_None/*false;*/;
   if (Res == pkgPackageManager::Completed)
      return Py_None /*true;*/;
      
   _system->Lock();
#endif

   // FIXME: open the cache here again
   
   return HandleErrors(Py_None);
}


static PyObject *PkgDepCacheGetCandidateVer(PyObject *Self,PyObject *Args)
{ 
   PkgDepCacheStruct &Struct = GetCpp<PkgDepCacheStruct>(Self);
   PyObject *PackageObj;
   PyObject *CandidateObj;
   if (PyArg_ParseTuple(Args,"O!",&PackageType,&PackageObj) == 0)
      return 0;

   pkgCache::PkgIterator &Pkg = GetCpp<pkgCache::PkgIterator>(PackageObj);
   pkgCache::VerIterator I = Struct.depcache->GetCandidateVer(Pkg);
   if(I.end()) {
      Py_INCREF(Py_None);
      return Py_None;
   }
   CandidateObj = CppOwnedPyObject_NEW<pkgCache::VerIterator>(PackageObj,&VersionType,I);

   return CandidateObj;
}

static PyObject *PkgDepCacheUpgrade(PyObject *Self,PyObject *Args)
{   
   PkgDepCacheStruct &Struct = GetCpp<PkgDepCacheStruct>(Self);

   char distUpgrade=0;
   if (PyArg_ParseTuple(Args,"|b",&distUpgrade) == 0)
      return 0;

   if(distUpgrade)
      pkgDistUpgrade(*Struct.depcache);
   else
      pkgAllUpgrade(*Struct.depcache);

   Py_INCREF(Py_None);
   return HandleErrors(Py_None);   
}

static PyObject *PkgDepCacheReadPinFile(PyObject *Self,PyObject *Args)
{   
   PkgDepCacheStruct &Struct = GetCpp<PkgDepCacheStruct>(Self);

   char *file=NULL;
   if (PyArg_ParseTuple(Args,"|s",&file) == 0)
      return 0;

   if(file == NULL) 
      ReadPinFile(*Struct.policy);
   else
      ReadPinFile(*Struct.policy, file);

   Py_INCREF(Py_None);
   return HandleErrors(Py_None);   
}


static PyObject *PkgDepCacheFixBroken(PyObject *Self,PyObject *Args)
{   
   PkgDepCacheStruct &Struct = GetCpp<PkgDepCacheStruct>(Self);

   if (PyArg_ParseTuple(Args,"") == 0)
      return 0;

   pkgFixBroken(*Struct.depcache);

   Py_INCREF(Py_None);
   return HandleErrors(Py_None);   
}


static PyObject *PkgDepCacheMarkKeep(PyObject *Self,PyObject *Args)
{   
   PkgDepCacheStruct &Struct = GetCpp<PkgDepCacheStruct>(Self);

   PyObject *PackageObj;
   if (PyArg_ParseTuple(Args,"O!",&PackageType,&PackageObj) == 0)
      return 0;

   pkgCache::PkgIterator &Pkg = GetCpp<pkgCache::PkgIterator>(PackageObj);
   Struct.depcache->MarkKeep(Pkg);

   Py_INCREF(Py_None);
   return HandleErrors(Py_None);   
}

static PyObject *PkgDepCacheMarkDelete(PyObject *Self,PyObject *Args)
{   
   PkgDepCacheStruct &Struct = GetCpp<PkgDepCacheStruct>(Self);

   PyObject *PackageObj;
   char purge = 0;
   if (PyArg_ParseTuple(Args,"O!|b",&PackageType,&PackageObj, &purge) == 0)
      return 0;

   pkgCache::PkgIterator &Pkg = GetCpp<pkgCache::PkgIterator>(PackageObj);
   Struct.depcache->MarkDelete(Pkg,purge);

   Py_INCREF(Py_None);
   return HandleErrors(Py_None);   
}

static PyObject *PkgDepCacheMarkInstall(PyObject *Self,PyObject *Args)
{   
   PkgDepCacheStruct &Struct = GetCpp<PkgDepCacheStruct>(Self);

   PyObject *PackageObj;
   if (PyArg_ParseTuple(Args,"O!",&PackageType,&PackageObj) == 0)
      return 0;

   pkgCache::PkgIterator &Pkg = GetCpp<pkgCache::PkgIterator>(PackageObj);
   Struct.depcache->MarkInstall(Pkg);

   Py_INCREF(Py_None);
   return HandleErrors(Py_None);   
}

static PyObject *PkgDepCacheIsUpgradable(PyObject *Self,PyObject *Args)
{   
   PkgDepCacheStruct &Struct = GetCpp<PkgDepCacheStruct>(Self);

   PyObject *PackageObj;
   if (PyArg_ParseTuple(Args,"O!",&PackageType,&PackageObj) == 0)
      return 0;

   pkgCache::PkgIterator &Pkg = GetCpp<pkgCache::PkgIterator>(PackageObj);
   pkgDepCache::StateCache &state = (*Struct.depcache)[Pkg];

   return HandleErrors(Py_BuildValue("b",state.Upgradable()));   
}

static PyObject *PkgDepCacheIsNowBroken(PyObject *Self,PyObject *Args)
{   
   PkgDepCacheStruct &Struct = GetCpp<PkgDepCacheStruct>(Self);

   PyObject *PackageObj;
   if (PyArg_ParseTuple(Args,"O!",&PackageType,&PackageObj) == 0)
      return 0;

   pkgCache::PkgIterator &Pkg = GetCpp<pkgCache::PkgIterator>(PackageObj);
   pkgDepCache::StateCache &state = (*Struct.depcache)[Pkg];

   return HandleErrors(Py_BuildValue("b",state.NowBroken()));   
}

static PyObject *PkgDepCacheIsInstBroken(PyObject *Self,PyObject *Args)
{   
   PkgDepCacheStruct &Struct = GetCpp<PkgDepCacheStruct>(Self);

   PyObject *PackageObj;
   if (PyArg_ParseTuple(Args,"O!",&PackageType,&PackageObj) == 0)
      return 0;

   pkgCache::PkgIterator &Pkg = GetCpp<pkgCache::PkgIterator>(PackageObj);
   pkgDepCache::StateCache &state = (*Struct.depcache)[Pkg];

   return HandleErrors(Py_BuildValue("b",state.InstBroken()));   
}


static PyObject *PkgDepCacheMarkedInstall(PyObject *Self,PyObject *Args)
{   
   PkgDepCacheStruct &Struct = GetCpp<PkgDepCacheStruct>(Self);

   PyObject *PackageObj;
   if (PyArg_ParseTuple(Args,"O!",&PackageType,&PackageObj) == 0)
      return 0;

   pkgCache::PkgIterator &Pkg = GetCpp<pkgCache::PkgIterator>(PackageObj);
   pkgDepCache::StateCache &state = (*Struct.depcache)[Pkg];

   return HandleErrors(Py_BuildValue("b",state.Install()));   
}

static PyObject *PkgDepCacheMarkedUpgrade(PyObject *Self,PyObject *Args)
{   
   PkgDepCacheStruct &Struct = GetCpp<PkgDepCacheStruct>(Self);

   PyObject *PackageObj;
   if (PyArg_ParseTuple(Args,"O!",&PackageType,&PackageObj) == 0)
      return 0;

   pkgCache::PkgIterator &Pkg = GetCpp<pkgCache::PkgIterator>(PackageObj);
   pkgDepCache::StateCache &state = (*Struct.depcache)[Pkg];

   return HandleErrors(Py_BuildValue("b",state.Upgrade()));   
}

static PyObject *PkgDepCacheMarkedDelete(PyObject *Self,PyObject *Args)
{   
   PkgDepCacheStruct &Struct = GetCpp<PkgDepCacheStruct>(Self);

   PyObject *PackageObj;
   if (PyArg_ParseTuple(Args,"O!",&PackageType,&PackageObj) == 0)
      return 0;

   pkgCache::PkgIterator &Pkg = GetCpp<pkgCache::PkgIterator>(PackageObj);
   pkgDepCache::StateCache &state = (*Struct.depcache)[Pkg];

   return HandleErrors(Py_BuildValue("b",state.Delete()));   
}

static PyObject *PkgDepCacheMarkedKeep(PyObject *Self,PyObject *Args)
{   
   PkgDepCacheStruct &Struct = GetCpp<PkgDepCacheStruct>(Self);

   PyObject *PackageObj;
   if (PyArg_ParseTuple(Args,"O!",&PackageType,&PackageObj) == 0)
      return 0;

   pkgCache::PkgIterator &Pkg = GetCpp<pkgCache::PkgIterator>(PackageObj);
   pkgDepCache::StateCache &state = (*Struct.depcache)[Pkg];

   return HandleErrors(Py_BuildValue("b",state.Keep()));   
}

static PyMethodDef PkgDepCacheMethods[] = 
{
   {"Init",PkgDepCacheInit,METH_VARARGS,"Init the depcache (done on construct automatically)"},
   {"GetCandidateVer",PkgDepCacheGetCandidateVer,METH_VARARGS,"Get candidate version"},

   // global cache operations
   {"Upgrade",PkgDepCacheUpgrade,METH_VARARGS,"Perform Upgrade (optional boolean argument if dist-upgrade should be performed)"},
   {"FixBroken",PkgDepCacheFixBroken,METH_VARARGS,"Fix broken packages"},
   {"ReadPinFile",PkgDepCacheReadPinFile,METH_VARARGS,"Read the pin policy"},
   // Manipulators
   {"MarkKeep",PkgDepCacheMarkKeep,METH_VARARGS,"Mark package for keep"},
   {"MarkDelete",PkgDepCacheMarkDelete,METH_VARARGS,"Mark package for delete (optional boolean argument if it should be purged)"},
   {"MarkInstall",PkgDepCacheMarkInstall,METH_VARARGS,"Mark package for Install"},
   // state information
   {"IsUpgradable",PkgDepCacheIsUpgradable,METH_VARARGS,"Is pkg upgradable"},
   {"IsNowBroken",PkgDepCacheIsNowBroken,METH_VARARGS,"Is pkg is now broken"},
   {"IsInstBroken",PkgDepCacheIsInstBroken,METH_VARARGS,"Is pkg broken on the current install"},
   {"MarkedInstall",PkgDepCacheMarkedInstall,METH_VARARGS,"Is pkg marked for install"},
   {"MarkedUpgrade",PkgDepCacheMarkedUpgrade,METH_VARARGS,"Is pkg marked for upgrade"},
   {"MarkedDelete",PkgDepCacheMarkedDelete,METH_VARARGS,"Is pkg marked for delete"},
   {"MarkedKeep",PkgDepCacheMarkedDelete,METH_VARARGS,"Is pkg marked for keep"},

   // Action
   {"Commit", PkgDepCacheCommit, METH_VARARGS, "Commit pending changes"},

   {}
};


static PyObject *DepCacheAttr(PyObject *Self,char *Name)
{
   PkgDepCacheStruct &Struct = GetCpp<PkgDepCacheStruct>(Self);

   // size querries
   if(strcmp("KeepCount",Name) == 0) 
      return Py_BuildValue("l", Struct.depcache->KeepCount());
   else if(strcmp("InstCount",Name) == 0) 
      return Py_BuildValue("l", Struct.depcache->InstCount());
   else if(strcmp("DelCount",Name) == 0) 
      return Py_BuildValue("l", Struct.depcache->DelCount());
   else if(strcmp("BrokenCount",Name) == 0) 
      return Py_BuildValue("l", Struct.depcache->BrokenCount());
   else if(strcmp("UsrSize",Name) == 0) 
      return Py_BuildValue("d", Struct.depcache->UsrSize());
   else if(strcmp("DebSize",Name) == 0) 
      return Py_BuildValue("d", Struct.depcache->DebSize());
   
   
   return Py_FindMethod(PkgDepCacheMethods,Self,Name);
}




PyTypeObject PkgDepCacheType =
{
   PyObject_HEAD_INIT(&PyType_Type)
   0,			                // ob_size
   "pkgDepCache",                          // tp_name
   sizeof(CppOwnedPyObject<PkgDepCacheStruct>),   // tp_basicsize
   0,                                   // tp_itemsize
   // Methods
   CppOwnedDealloc<PkgDepCacheStruct>,        // tp_dealloc
   0,                                   // tp_print
   DepCacheAttr,                           // tp_getattr
   0,                                   // tp_setattr
   0,                                   // tp_compare
   0,                                   // tp_repr
   0,                                   // tp_as_number
   0,                                   // tp_as_sequence
   0,	                                // tp_as_mapping
   0,                                   // tp_hash
};


PyObject *GetDepCache(PyObject *Self,PyObject *Args)
{
   PyObject *Owner;
   if (PyArg_ParseTuple(Args,"O!",&PkgCacheType,&Owner) == 0)
      return 0;

   PyObject *DepCachePyObj;
   DepCachePyObj = CppOwnedPyObject_NEW<PkgDepCacheStruct>(Owner,
							   &PkgDepCacheType,
							   GetCpp<pkgCache *>(Owner));
   HandleErrors(DepCachePyObj);
   PkgDepCacheStruct &Struct = GetCpp<PkgDepCacheStruct>(DepCachePyObj);   

   return DepCachePyObj;
}




									/*}}}*/

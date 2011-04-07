/*
 * Copyright (c) 2006-2007 Apple Inc. All Rights Reserved.
 * 
 * @APPLE_LICENSE_HEADER_START@
 * 
 * This file contains Original Code and/or Modifications of Original Code
 * as defined in and that are subject to the Apple Public Source License
 * Version 2.0 (the 'License'). You may not use this file except in
 * compliance with the License. Please obtain a copy of the License at
 * http://www.opensource.apple.com/apsl/ and read it before using this
 * file.
 * 
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
 * Please see the License for the specific language governing rights and
 * limitations under the License.
 * 
 * @APPLE_LICENSE_HEADER_END@
 */

//
// diskrep - disk representations of code
//
#ifndef _H_DISKREP
#define _H_DISKREP

#include "cs.h"
#include "codedirectory.h"
#include "cdbuilder.h"
#include "requirement.h"
#include "resources.h"
#include <security_utilities/macho++.h>		// for class Architecture
#include <security_utilities/refcount.h>
#include <security_utilities/superblob.h>
#include <CoreFoundation/CFData.h>

namespace Security {
namespace CodeSigning {


//
// DiskRep is an abstract interface to code somewhere located by
// a file system path. It presents the ability to read and write
// Code Signing-related information about such code without exposing
// the details of the storage locations or formats.
//
class DiskRep : public RefCount {
public:
	DiskRep();
	virtual ~DiskRep();
	virtual DiskRep *base();
	virtual CFDataRef component(CodeDirectory::SpecialSlot slot) = 0; // fetch component
	virtual CFDataRef identification() = 0;					// binary lookup identifier
	virtual std::string mainExecutablePath() = 0;			// path to main executable
	virtual CFURLRef canonicalPath() = 0;					// path to whole code
	virtual std::string recommendedIdentifier() = 0;		// default identifier
	virtual std::string resourcesRootPath();				// resource directory if any
	virtual CFDictionaryRef defaultResourceRules();			// default resource rules
	virtual void adjustResources(ResourceBuilder &builder);	// adjust resource rule set
	virtual const Requirements *defaultRequirements(const Architecture *arch); // default internal requirements
	virtual Universal *mainExecutableImage();				// binary if Mach-O/Universal
	virtual size_t pageSize();								// default main executable page size
	virtual size_t signingBase();							// start offset of signed area in main executable
	virtual size_t signingLimit() = 0;						// size of signed area in main executable
	virtual std::string format() = 0;						// human-readable type string
	virtual CFArrayRef modifiedFiles();						// list of files modified by signing
	virtual UnixPlusPlus::FileDesc &fd() = 0;				// a cached fd for main executable file
	virtual void flush();									// flush caches (refetch as needed)
	
	bool mainExecutableIsMachO() { return mainExecutableImage() != NULL; }
	
	// shorthands
	CFDataRef codeDirectory()	{ return component(cdCodeDirectorySlot); }
	CFDataRef signature()		{ return component(cdSignatureSlot); }

public:
	class Writer;
	virtual Writer *writer();

public:
	struct Context {
		Context() : arch(Architecture::none), offset(0), fileOnly(false) { }
		Architecture arch;			// explicit architecture
		off_t offset;				// explicit file offset
		bool fileOnly;				// only consider single-file representations
	};

	static DiskRep *bestGuess(const char *path, const Context *ctx = NULL); // canonical heuristic, any path
	static DiskRep *bestFileGuess(const char *path, const Context *ctx = NULL); // ctx (if any) + fileOnly
	static DiskRep *bestGuess(const char *path, size_t archOffset); // Mach-O at given file offset only
	
	static DiskRep *bestGuess(const std::string &path, const Context *ctx = NULL)
		{ return bestGuess(path.c_str(), ctx); }
	static DiskRep *bestGuess(const std::string &path, size_t archOffset) { return bestGuess(path.c_str(), archOffset); }
	static DiskRep *bestFileGuess(const std::string &path, const Context *ctx = NULL) { return bestFileGuess(path.c_str(), ctx); }
	
public:
	static const size_t segmentedPageSize = 4096;	// default page size for system-paged signatures
	static const size_t monolithicPageSize = 0;		// default page size for non-Mach-O executables
};


//
// Write-access objects.
// At this layer they are quite abstract, carrying just the functionality needed
// for the signing machinery to place data wherever it should go. Each DiskRep subclass
// that supports writing signing data to a place inside the code needs to implement
// a subclass of Writer and return an instance in the DiskRep::writer() method when asked.
//
class DiskRep::Writer : public RefCount {
public:
	Writer(uint32_t attrs = 0);
	virtual ~Writer();
	virtual void component(CodeDirectory::SpecialSlot slot, CFDataRef data) = 0;
	virtual uint32_t attributes() const;
	virtual void addDiscretionary(CodeDirectory::Builder &builder);
	virtual void remove();
	virtual void flush();

	bool attribute(uint32_t attr) const		{ return mAttributes & attr; }
	
	void signature(CFDataRef data)			{ component(cdSignatureSlot, data); }
	void codeDirectory(const CodeDirectory *cd)
		{ component(cdCodeDirectorySlot, CFTempData(cd->data(), cd->length())); }
	
private:
	Architecture mArch;
	uint32_t mAttributes;
};

//
// Writer attributes. Defaults should be off-bits.
//
enum {
	writerLastResort = 0x0001,			// prefers not to store attributes itself
	writerNoGlobal = 0x0002,			// has only per-architecture storage
};


//
// A prefix DiskRep that filters (only) signature-dependent behavior and passes
// all code-dependent behavior off to an underlying (different) DiskRep.
// FilterRep subclasses are typically "stacked" on top of their base DiskRep, and
// then used in their place.
//
class FilterRep : public DiskRep {
public:
	FilterRep(DiskRep *orig) : mOriginal(orig) { }
	
	DiskRep *base()							{ return mOriginal; }
	
	// things that look at signature components are filtered
	CFDataRef component(CodeDirectory::SpecialSlot slot) = 0;

	// the rest of the virtual behavior devolves on the original DiskRep
	CFDataRef identification()				{ return mOriginal->identification(); }
	std::string mainExecutablePath()		{ return mOriginal->mainExecutablePath(); }
	CFURLRef canonicalPath()				{ return mOriginal->canonicalPath(); }
	std::string recommendedIdentifier()		{ return mOriginal->recommendedIdentifier(); }
	std::string resourcesRootPath()			{ return mOriginal->resourcesRootPath(); }
	CFDictionaryRef defaultResourceRules()	{ return mOriginal->defaultResourceRules(); }
	Universal *mainExecutableImage()		{ return mOriginal->mainExecutableImage(); }
	size_t signingBase()					{ return mOriginal->signingBase(); }
	size_t signingLimit()					{ return mOriginal->signingLimit(); }
	std::string format()					{ return mOriginal->format(); }
	UnixPlusPlus::FileDesc &fd()			{ return mOriginal->fd(); }
	void flush()							{ return mOriginal->flush(); }

private:
	RefPointer<DiskRep> mOriginal;			// underlying representation
};


} // end namespace CodeSigning
} // end namespace Security

#endif // !_H_DISKREP

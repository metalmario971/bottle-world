/**
*
*    @file TextDataFile.h
*    @date November 22, 2014
*    @author Derek Page
*
*    � 2014 
*
*
*/
#pragma once
#ifndef __BASEDATAFILE_210628895103872956110227_H__
#define __BASEDATAFILE_210628895103872956110227_H__


//#include "../base/ExLib.h"
//#include "../library/LibraryHeader.h"
#include "../base/TextParser.h"

namespace Game {
/**
*    @class TextDataFile
*    @brief Base class for in-memory text files.
*/
class TextDataFile : public VirtualMemory {
protected:
    t_string _fileName;
    t_string _fileLoc;
    char* _fileData = nullptr;
    int64_t _fileSize = 0;
    std::vector<t_string> _vecMessages;



    //Parse Helpers
    int32_t _curIdx;// gets IDX of current token array value

    // Override these methods.
    virtual void preLoad()=0;
    virtual void parse(char* buf, int64_t filesize)=0;
    virtual void postLoad(bool success)=0;

    void msg(t_string msg, bool error = false);
public:

    t_string& getFileLoc(){ return _fileLoc; } //This is used don't change
    void loadAndParse();// - If empty is specified then we use _fileLoc
    void loadAndParse(t_string& loc);// - If empty is specified then we use _fileLoc
    virtual void save(t_string& loc);

    //+-- CTOR/DTOR --+
    TextDataFile();
    TextDataFile(t_string& loc);
    virtual ~TextDataFile() override ;
};

}//ns game



#endif
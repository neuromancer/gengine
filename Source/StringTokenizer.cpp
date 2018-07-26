//
// StringTokenizer.cpp
//
// Clark Kromenaker
//
#include "StringTokenizer.h"

StringTokenizer::StringTokenizer(std::string str, std::initializer_list<char> splitChars)
{
    int startIndex = 0;
    for(int i = 0; i < str.size(); i++)
    {
        for(auto& splitChar : splitChars)
        {
            if(str[i] == splitChar)
            {
                if(i - startIndex > 1)
                {
                    mTokens.push_back(str.substr(startIndex, i - startIndex));
                }
                startIndex = i + 1;
            }
        }
    }
    
    if(str.size() - startIndex > 1)
    {
        mTokens.push_back(str.substr(startIndex, str.size() - startIndex));
    }
}

std::string StringTokenizer::GetNext()
{
    if(mIndex < mTokens.size())
    {
        return mTokens[mIndex++];
    }
    else
    {
        return "";
    }
}

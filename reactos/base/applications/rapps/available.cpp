/*
 * PROJECT:         ReactOS Applications Manager
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            base/applications/rapps/available.cpp
 * PURPOSE:         Classes for working with available applications
 * PROGRAMMERS:     Dmitry Chapyshev           (dmitry@reactos.org)
 *                  Ismael Ferreras Morezuelas (swyterzone+ros@gmail.com)
 *                  Alexander Shaposhnikov     (chaez.san@gmail.com)
 */
#include "defines.h"

#include "available.h"
#include "misc.h"
#include "dialogs.h"

#include <atlcoll.h>
#include <atlsimpcoll.h>
#include <atlstr.h>

 // CAvailableApplicationInfo
CAvailableApplicationInfo::CAvailableApplicationInfo(const ATL::CStringW& sFileNameParam)
    : m_Parser(sFileNameParam)
{
    LicenseType = LICENSE_TYPE::None;
    sFileName = sFileNameParam;

    RetrieveGeneralInfo();
}

VOID CAvailableApplicationInfo::RefreshAppInfo()
{
    if (szUrlDownload.IsEmpty())
    {
        RetrieveGeneralInfo();
    }
}

// Lazily load general info from the file
VOID CAvailableApplicationInfo::RetrieveGeneralInfo()
{
    Category = m_Parser.GetInt(L"Category");

    if (!GetString(L"Name", szName)
        || !GetString(L"URLDownload", szUrlDownload))
    {
        return;
    }

    GetString(L"RegName", szRegName);
    GetString(L"Version", szVersion);
    GetString(L"License", szLicense);
    GetString(L"Description", szDesc);
    GetString(L"Size", szSize);
    GetString(L"URLSite", szUrlSite);
    GetString(L"CDPath", szCDPath);
    GetString(L"Language", szRegName);
    GetString(L"SHA1", szSHA1);

    RetrieveLicenseType();
    RetrieveLanguages();
    RetrieveInstalledStatus();
    if (m_IsInstalled)
    {
        RetrieveInstalledVersion();
    }
}

VOID CAvailableApplicationInfo::RetrieveInstalledStatus()
{
    m_IsInstalled = ::GetInstalledVersion(NULL, szRegName)
        || ::GetInstalledVersion(NULL, szName);
}

VOID CAvailableApplicationInfo::RetrieveInstalledVersion()
{
    m_HasInstalledVersion = ::GetInstalledVersion(&szInstalledVersion, szRegName)
        || ::GetInstalledVersion(&szInstalledVersion, szName);
}

VOID CAvailableApplicationInfo::RetrieveLanguages()
{
    const WCHAR cDelimiter = L'|';
    ATL::CStringW szBuffer;

    // TODO: Get multiline parameter
    if (!m_Parser.GetString(L"Languages", szBuffer))
    {
        m_HasLanguageInfo = FALSE;
        return;
    }

    // Parse parameter string
    ATL::CStringW m_szLocale;
    INT iLCID;
    for (INT i = 0; szBuffer[i] != UNICODE_NULL; ++i)
    {
        if (szBuffer[i] != cDelimiter && szBuffer[i] != L'\n')
        {
            m_szLocale += szBuffer[i];
        }
        else
        {
            if (StrToIntExW(m_szLocale.GetString(), STIF_DEFAULT, &iLCID))
            {
                Languages.Add(static_cast<LCID>(iLCID));
                m_szLocale.Empty();
            }
        }
    }

    // For the text after delimiter
    if (!m_szLocale.IsEmpty())
    {
        if (StrToIntExW(m_szLocale.GetString(), STIF_DEFAULT, &iLCID))
        {
            Languages.Add(static_cast<LCID>(iLCID));
        }
    }

    m_HasLanguageInfo = TRUE;
}

VOID CAvailableApplicationInfo::RetrieveLicenseType()
{
    INT IntBuffer = m_Parser.GetInt(L"LicenseType");

    if (IntBuffer < 0 || IntBuffer > LICENSE_TYPE::Max)
    {
        LicenseType = LICENSE_TYPE::None;
    }
    else
    {
        LicenseType = (LICENSE_TYPE) IntBuffer;
    }
}

BOOL CAvailableApplicationInfo::FindInLanguages(LCID what) const
{
    if (!m_HasLanguageInfo)
    {
        return FALSE;
    }

    //Find locale code in the list
    const INT nLanguagesSize = Languages.GetSize();
    for (INT i = 0; i < nLanguagesSize; ++i)
    {
        if (Languages[i] == what)
        {
            return TRUE;
        }
    }

    return FALSE;
}

BOOL CAvailableApplicationInfo::HasLanguageInfo() const
{
    return m_HasLanguageInfo;
}

BOOL CAvailableApplicationInfo::HasNativeLanguage() const
{
    return FindInLanguages(GetUserDefaultLCID());
}

BOOL CAvailableApplicationInfo::HasEnglishLanguage() const
{
    return FindInLanguages(MAKELCID(MAKELANGID(LANG_ENGLISH, SUBLANG_DEFAULT), SORT_DEFAULT));
}

BOOL CAvailableApplicationInfo::IsInstalled() const
{
    return m_IsInstalled;
}

BOOL CAvailableApplicationInfo::HasInstalledVersion() const
{
    return m_HasInstalledVersion;
}

BOOL CAvailableApplicationInfo::HasUpdate() const
{
    return (szInstalledVersion.Compare(szVersion) < 0) ? TRUE : FALSE;
}

VOID CAvailableApplicationInfo::SetLastWriteTime(FILETIME* ftTime)
{
    RtlCopyMemory(&ftCacheStamp, ftTime, sizeof(FILETIME));
}

inline BOOL CAvailableApplicationInfo::GetString(LPCWSTR lpKeyName, ATL::CStringW& ReturnedString)
{
    if (!m_Parser.GetString(lpKeyName, ReturnedString))
    {
        ReturnedString.Empty();
        return FALSE;
    }
    return TRUE;
}
// CAvailableApplicationInfo 

// CAvailableApps
ATL::CStringW CAvailableApps::m_szPath;
ATL::CStringW CAvailableApps::m_szCabPath;
ATL::CStringW CAvailableApps::m_szAppsPath;
ATL::CStringW CAvailableApps::m_szSearchPath;

BOOL CAvailableApps::InitializeStaticStrings()
{

    if (!m_szPath.IsEmpty())
    {
        // strings are filled
        return TRUE;
    }

    //FIXME: maybe provide a fallback?
    if (GetStorageDirectory(m_szPath))
    {
        m_szAppsPath = m_szPath + L"\\rapps\\";
        m_szCabPath = m_szPath + L"\\rappmgr.cab";
        m_szSearchPath = m_szAppsPath + L"*.txt";
        return TRUE;
    }

    return FALSE;
}

CAvailableApps::CAvailableApps()
{
    //set all paths
    InitializeStaticStrings();
}

VOID CAvailableApps::FreeCachedEntries()
{
    POSITION InfoListPosition = m_InfoList.GetHeadPosition();

    /* loop and deallocate all the cached app infos in the list */
    while (InfoListPosition)
    {
        CAvailableApplicationInfo* Info = m_InfoList.GetNext(InfoListPosition);
        delete Info;
    }

    m_InfoList.RemoveAll();
}

VOID CAvailableApps::DeleteCurrentAppsDB()
{
    HANDLE hFind = INVALID_HANDLE_VALUE;
    WIN32_FIND_DATAW FindFileData;

    if (!InitializeStaticStrings())
    {
        return;
    }

    hFind = FindFirstFileW(m_szSearchPath.GetString(), &FindFileData);

    if (hFind != INVALID_HANDLE_VALUE)
    {
        ATL::CStringW szTmp;
        do
        {
            szTmp = m_szAppsPath + FindFileData.cFileName;
            DeleteFileW(szTmp.GetString());
        } while (FindNextFileW(hFind, &FindFileData) != 0);
        FindClose(hFind);
    }

    RemoveDirectoryW(m_szAppsPath);
    RemoveDirectoryW(m_szPath);
}

BOOL CAvailableApps::UpdateAppsDB()
{
    HANDLE hFind = INVALID_HANDLE_VALUE;
    WIN32_FIND_DATAW FindFileData;

    if (!InitializeStaticStrings())
    {
        return FALSE;
    }

    if (!CreateDirectoryW(m_szPath.GetString(), NULL) && GetLastError() != ERROR_ALREADY_EXISTS)
    {
        return FALSE;
    }

    //if there are some files in the db folder - we're good
    hFind = FindFirstFileW(m_szSearchPath.GetString(), &FindFileData);
    if (hFind != INVALID_HANDLE_VALUE)
    {
        return TRUE;
    }

    CDownloadManager::DownloadApplicationsDB(APPLICATION_DATABASE_URL);

    if (!ExtractFilesFromCab(m_szCabPath, m_szAppsPath))
    {
        return FALSE;
    }

    DeleteFileW(m_szCabPath.GetString());

    return TRUE;
}

BOOL CAvailableApps::ForceUpdateAppsDB()
{
    DeleteCurrentAppsDB();
    return UpdateAppsDB();
}

BOOL CAvailableApps::EnumAvailableApplications(INT EnumType, AVAILENUMPROC lpEnumProc)
{

    HANDLE hFind = INVALID_HANDLE_VALUE;
    WIN32_FIND_DATAW FindFileData;

    hFind = FindFirstFileW(m_szSearchPath.GetString(), &FindFileData);

    if (hFind == INVALID_HANDLE_VALUE)
    {
        //no db yet
        return FALSE;
    }

    do
    {
        // loop for all the cached entries
        POSITION CurrentListPosition = m_InfoList.GetHeadPosition();
        CAvailableApplicationInfo* Info = NULL;

        while (CurrentListPosition != NULL)
        {
            POSITION LastListPosition = CurrentListPosition;
            Info = m_InfoList.GetNext(CurrentListPosition);

            // do we already have this entry in cache?
            if (Info->sFileName == FindFileData.cFileName)
            {
                // is it current enough, or the file has been modified since our last time here?
                if (CompareFileTime(&FindFileData.ftLastWriteTime, &Info->ftCacheStamp) == 1)
                {
                    // recreate our cache, this is the slow path
                    m_InfoList.RemoveAt(LastListPosition);

                    delete Info;
                    Info = NULL;
                    break;
                }
                else
                {
                    // speedy path, compare directly, we already have the data
                    goto skip_if_cached;
                }
            }
        }

        // create a new entry
        Info = new CAvailableApplicationInfo(FindFileData.cFileName);

        // set a timestamp for the next time
        Info->SetLastWriteTime(&FindFileData.ftLastWriteTime);
        m_InfoList.AddTail(Info);

skip_if_cached:
        if (Info->Category == FALSE)
            continue;

        if (EnumType != Info->Category && EnumType != ENUM_ALL_AVAILABLE)
            continue;

        Info->RefreshAppInfo();

        if (lpEnumProc)
            lpEnumProc(static_cast<PAPPLICATION_INFO>(Info), m_szAppsPath.GetString());

    } while (FindNextFileW(hFind, &FindFileData) != 0);

    FindClose(hFind);
    return TRUE;
}

const PAPPLICATION_INFO CAvailableApps::FindInfo(const ATL::CStringW& szAppName)
{
    if (m_InfoList.IsEmpty())
    {
        return NULL;
    }

    // linear search
    POSITION CurrentListPosition = m_InfoList.GetHeadPosition();
    PAPPLICATION_INFO info;
    while (CurrentListPosition != NULL)
    {
        info = m_InfoList.GetNext(CurrentListPosition);
        if (info->szName == szAppName)
        {
            return info;
        }
    }
    return NULL;
}

ATL::CSimpleArray<PAPPLICATION_INFO> CAvailableApps::FindInfoList(const ATL::CSimpleArray<ATL::CStringW> &arrAppsNames)
{
    ATL::CSimpleArray<PAPPLICATION_INFO> result;
    for (INT i = 0; i < arrAppsNames.GetSize(); ++i)
    {
        PAPPLICATION_INFO Info = FindInfo(arrAppsNames[i]);
        if (Info)
        {
            result.Add(Info);
        }
    }
    return result;
}

const ATL::CStringW& CAvailableApps::GetFolderPath()
{
    return m_szPath;
}

const ATL::CStringW& CAvailableApps::GetAppPath()
{
    return m_szAppsPath;
}

const ATL::CStringW& CAvailableApps::GetCabPath()
{
    return m_szCabPath;
}

const LPCWSTR CAvailableApps::GetFolderPathString()
{
    return m_szPath.GetString();
}

const LPCWSTR CAvailableApps::GetAppPathString()
{
    return m_szPath.GetString();
}

const LPCWSTR CAvailableApps::GetCabPathString()
{
    return m_szPath.GetString();
}
// CAvailableApps

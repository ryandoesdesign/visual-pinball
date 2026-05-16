#include "core/stdafx.h"
#include "codeview.h"


#ifdef __LIBVPINBALL__
#include "lib/src/VPinballLib.h"
#endif

#include <sstream>
#include <climits>

#include <fstream>

#include "ui/win/PinTableWnd.h"
#include "ui/win/WinEditor.h"

static constexpr int LAST_ERROR_WIDGET_HEIGHT = 256;

//Scintilla Lexer parses only lower case unless otherwise told
static constexpr char vbsReservedWords[] =
"and as byref byval case call const "
"continue dim do each else elseif end error exit false for function global "
"goto if in loop me new next not nothing on optional or private public "
"redim rem resume select set sub then to true type while with "
"boolean byte currency date double integer long object single string type "
"variant option explicit randomize";

static const string VBvalidChars("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_"s);

INT_PTR CALLBACK CVPrefProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);


constexpr __forceinline bool IsWhitespace(const char ch) { return (ch == ' ' || ch == 9 /*tab*/); }

inline void RemovePadding(string& line)
{
   const size_t LL = line.length();
   size_t Pos = line.find_first_not_of("\n\r\t ,");
   if (Pos == string::npos)
   {
      line.clear();
      return;
   }

   if (Pos > 0)
   {
      if ((SSIZE_T)(LL - Pos) < 1)
         return;
      line = line.substr(Pos, (LL - Pos));
   }

   Pos = line.find_last_not_of("\n\r\t ,");
   if (Pos != string::npos)
   {
      if (Pos < 1)
         return;
      line = line.erase(Pos + 1);
   }
}

inline string ParseRemoveVBSLineComments(string& line)
{
   const size_t commentIdx = line.find('\'');
   if (commentIdx == string::npos)
      return string();
   string RetVal = line.substr(commentIdx + 1);
   RemovePadding(RetVal);
   if (commentIdx > 0)
      line.resize(commentIdx);
   else
      line.clear();
   return RetVal;
}


CodeViewer::CodeViewer(PinTable* psh)
{
   m_table = psh;

   szFindString[0] = '\0';
   szReplaceString[0] = '\0';
   szCaretTextBuff[0] = '\0';


   m_findreplaceold.lStructSize = 0; // So we know nothing has been searched for yet
}

CodeViewer::~CodeViewer()
{
   Destroy();

   for (size_t i = 0; i < m_vcvd.size(); ++i)
      delete m_vcvd[i];

}

// strSearchData has to be lower case
template<bool uniqueKey> // otherwise keyName
static int UDKeyIndexHelper(const fi_vector<UserData>& ListIn, const string& strSearchData, int& curPosOut)
{
	const int ListSize = (int)ListIn.size();
	curPosOut = 1u << 30;
	while (!(curPosOut & ListSize) && (curPosOut > 1))
		curPosOut >>= 1;
	int iJumpDelta = curPosOut >> 1;
	--curPosOut; // Zero Base
	while (true)
	{
		const int result = (curPosOut >= ListSize) ? -1 : strSearchData.compare(uniqueKey ? ListIn[curPosOut].m_uniqueKey : lowerCase(ListIn[curPosOut].m_keyName));
		if (iJumpDelta == 0 || result == 0) return result;
		curPosOut = (result < 0) ? (curPosOut - iJumpDelta) : (curPosOut + iJumpDelta);
		iJumpDelta >>= 1;
	}
}

//true:  Returns current Index of strIn in ListIn based on m_uniqueKey, or -1 if not found
//false: Returns current Index of strIn in ListIn based on m_keyName,   or -1 if not found
template <bool uniqueKey> // otherwise keyName
static int UDKeyIndex(const fi_vector<UserData>& ListIn, const string& strIn)
{
	if (strIn.empty() || ListIn.empty()) return -1;

	int iCurPos;
	const int result = UDKeyIndexHelper<uniqueKey>(ListIn, lowerCase(strIn), iCurPos);

	///TODO: needs to consider children?
	return (result == 0) ? iCurPos : -1;
}

/*	FindUD - Now a human Search!
 0 =Found set to point at UD in list.
-1 =Not Found 
 1 =Not Found
-2 =Zero Length string or error
strSearchData has to be lower case */
static int FindUD(const fi_vector<UserData>& ListIn, const string& strSearchData, int& Pos)
{
	if (strSearchData.empty() || ListIn.empty()) return -2;

	Pos = -1;
	const int KeyResult = UDKeyIndexHelper<true>(ListIn, strSearchData, Pos);

	//If it's a top level construct it will have no parents and therefore have a unique key.
	if (KeyResult == 0) return 0;

	//Now see if it's in the Name list
	//Jumpdelta should be initialized to the maximum count of an individual key name
	//But for the moment the biggest is 64 x's in AMH
	Pos += KeyResult; //Start very close to the result of key search
	if (Pos < 0) Pos = 0;
	//Find the start of other instances of strSearchData by crawling up list
	//Usually (but not always) UDKeyIndexHelper<true> returns top of the list so its fast
	const size_t SearchWidth = strSearchData.length();
	do
	{
		--Pos;
	} while (Pos >= 0 && ListIn[Pos].m_uniqueKey.compare(0, SearchWidth, strSearchData) == 0);
	++Pos;
	// now walk down list of Keynames looking for what we want.
	if (Pos >= (int)ListIn.size())
		return -1;
	int result;
	string lc_keyName = lowerCase(ListIn[Pos].m_keyName);
	do 
	{
		result = lc_keyName.compare(strSearchData); 
		if (result == 0) return 0; //Found
		++Pos;
		if (Pos == (int)ListIn.size()) return result;

		lc_keyName = lowerCase(ListIn[Pos].m_keyName);
		result = lc_keyName.compare(0, SearchWidth, strSearchData);
	} while (result == 0); //EO SubList

	return result;
}

//Assumes case insensitive sorted list
//Returns index or insertion point (-1 == error)
static size_t FindOrInsertUD(fi_vector<UserData>& ListIn, const UserData& udIn)
{
	if (ListIn.empty()) // First in
	{
		ListIn.push_back(udIn);
		return 0;
	}

	int Pos = 0;
	const int KeyFound = udIn.m_uniqueKey.empty() ? -2 : UDKeyIndexHelper<true>(ListIn, udIn.m_uniqueKey, Pos);
	if (KeyFound == 0)
	{
		//Same name, different parents?
		const fi_vector<UserData>::const_iterator iterFound = ListIn.begin() + Pos;
		const int ParentResult = udIn.m_uniqueParent.compare(iterFound->m_uniqueParent);
		if (ParentResult == -1)
			ListIn.insert(iterFound, udIn);
		else if (ParentResult == 1)
		{
			ListIn.insert(iterFound+1, udIn);
			++Pos;
		}
		else
		{
			// detect/warn about duplicate subs/functions (at least rudimentary)
         if (g_pvp && g_pvp->GetActiveTableEditor() && g_pvp->GetActiveTableEditor()->m_pcv->m_warn_on_dupes &&
			    (udIn.eTyping == eSub || udIn.eTyping == eFunction) && // only check subs and functions
			    (iterFound->m_lineNum != udIn.m_lineNum)) // use this simple check as dupe test: are the keys on different lines?
			{
            g_pvp->GetActiveTableEditor()->m_pcv->m_warn_on_dupes = false;
			}

			// assign again, as e.g. line of func/sub/var could have been changed by other updates
			ListIn[Pos] = udIn;
		}
		return Pos;
	}

	if (KeyFound == -1) //insert before, somewhere in the middle
	{
		ListIn.insert(ListIn.begin() + Pos, udIn);
		return Pos;
	}
	else if (KeyFound == 1) //insert above last element - Special case 
	{
		ListIn.insert(ListIn.begin() + (Pos+1), udIn);
		return Pos+1;
	}
	else if ((ListIn.begin() + Pos) == (ListIn.end() - 1))
	{ //insert at end
		ListIn.push_back(udIn);
		return ListIn.size() - 1; //Zero Base
	}
	return -1;
}

// Needs speeding up.
// can potentially return a static variable, i.e. use the pointer before the next call
static const UserData* GetUDfromUniqueKey(const fi_vector<UserData>& ListIn, const string& UniKey)
{
	static UserData retUserData;
	retUserData.eTyping = eUnknown;
	const size_t ListSize = ListIn.size();
	for (size_t i = 0; i < ListSize; ++i)
		if (UniKey == ListIn[i].m_uniqueKey)
		{
			if (ListIn[i].eTyping != eUnknown)
				return &ListIn[i];
			retUserData = ListIn[i];
		}
	return &retUserData;
}

//TODO: Needs speeding up.
static size_t GetUDIdxfromUniqueKey(const fi_vector<UserData>& ListIn, const string& UniKey)
{
	const size_t ListSize = ListIn.size();
	for (size_t i = 0; i < ListSize; ++i)
		if (UniKey == ListIn[i].m_uniqueKey)
			return i;
	return -1;
}

//Finds the closest UD from CurrentLine in ListIn
//On entry CurrentIdx must be set to the UD in the line
static int FindClosestUD(const fi_vector<UserData>& ListIn, const int CurrentLine, const int CurrentIdx)
{
	const string strSearchData = lowerCase(ListIn[CurrentIdx].m_keyName);
	const size_t SearchWidth = strSearchData.length();
	//Find the start of other instances of strIn by crawling up list
	int iNewPos = CurrentIdx;
	do
	{
		--iNewPos;
	} while (iNewPos >= 0 && ListIn[iNewPos].m_uniqueKey.compare(0, SearchWidth, strSearchData) == 0);
	++iNewPos;
	//Now at top of list
	//find nearest definition above current line
	//int ClosestLineNum = 0;
	int ClosestPos = CurrentIdx;
	int Delta = INT_MIN;
	string lc_keyName = lowerCase(ListIn[iNewPos].m_keyName);
	do
	{
		const int NewLineNum = ListIn[iNewPos].m_lineNum;
		const int NewDelta = NewLineNum - CurrentLine;
		if (NewDelta >= Delta && NewLineNum <= CurrentLine && lc_keyName == strSearchData)
		{
			Delta = NewDelta;
			//ClosestLineNum = NewLineNum;
			ClosestPos = iNewPos;
		}
		++iNewPos;
		if (iNewPos == (int)ListIn.size())
			break;
		lc_keyName = lowerCase(ListIn[iNewPos].m_keyName);
	} while (lc_keyName.compare(0, SearchWidth, strSearchData) == 0);
	//--iNewPos;
	return ClosestPos;
}

// returns true if inserted, false if already in list
static bool FindOrInsertStringIntoAutolist(vector<string>& ListIn, const string &strIn)
{
	//First in the list
	if (ListIn.empty())
	{
		ListIn.push_back(strIn);
		return true;
	}
	const unsigned int ListSize = (unsigned int)ListIn.size();
	unsigned int iNewPos = 1u << 31;
	while (!(iNewPos & ListSize) && (iNewPos > 1))
		iNewPos >>= 1;
	int iJumpDelta = iNewPos >> 1;
	--iNewPos; //Zero Base
	const string strSearchData = lowerCase(strIn);
	unsigned int iCurPos;
	int result;
	while (true)
	{
		iCurPos = iNewPos;
		result = (iCurPos >= ListSize) ? -1 : strSearchData.compare(lowerCase(ListIn[iCurPos]));
		if (result == 0) return false; // Already in list
		if (iJumpDelta == 0) break;
		iNewPos = (result < 0) ? (iCurPos - iJumpDelta) : (iCurPos + iJumpDelta);
		iJumpDelta >>= 1;
	}

	const vector<string>::const_iterator i = ListIn.begin() + iCurPos;

	if (result == -1) //insert before, somewhere in the middle
	{
		ListIn.insert(i, strIn);
		return true;
	}

	if (i == (ListIn.end() - 1)) //insert above last element - Special case
	{
		ListIn.push_back(strIn);
		return true;
	}

	if (result == 1)
	{
		ListIn.insert(i+1, strIn);
		return true;
	}

	return false; //Oh pop poop, never should hit here.
}

//
//
//

static void GetRange(const HWND hwndScintilla, const size_t start, const size_t end, char * const text)
{
}

// buf must be >= MAX_FIND_LENGTH chars long
// returns length of word
size_t CodeViewer::GetWordUnderCaret(char *buf)
{
   return 0;
}

void CodeViewer::SetClean(const SaveDirtyState sds)
{
}

void CodeViewer::OnScriptError(ScriptInterpreter::ErrorType type, int line, int column, const string& description, const vector<string>& stackDump)
{
}

HRESULT CodeViewer::AddItem(IScriptable * const piscript, const bool global)
{
   CodeViewDispatch * const pcvd = new CodeViewDispatch();

   pcvd->m_wName = piscript->get_Name();
   pcvd->m_pdisp = piscript->GetIDispatch();
   pcvd->m_pdisp->QueryInterface(IID_IUnknown, (void **)&pcvd->m_punk);
   pcvd->m_punk->Release();
   pcvd->m_global = global;

   if (m_vcvd.GetSortedIndex(pcvd->m_wName) != -1)
   {
      delete pcvd;
      return E_FAIL;
   }

   m_vcvd.AddSortedString(pcvd);

   // Add item to dropdown
   string szT = MakeString(pcvd->m_wName);

   ITypeInfo* ti;
   if (SUCCEEDED(piscript->GetIDispatch()->GetTypeInfo(NULL, NULL, &ti))) {
      BSTR bstrTypeName;
      if (SUCCEEDED(ti->GetDocumentation(MEMBERID_NIL, &bstrTypeName, NULL, NULL, NULL))) {
         PLOGD << "type=" << MakeString(bstrTypeName) << ", name=" << szT;
         SysFreeString(bstrTypeName);
      }
      ti->Release();
   }
   //AndyS - WIP insert new item into autocomplete list??
   return S_OK;
}

void CodeViewer::RemoveItem(IScriptable * const piscript)
{
   const wstring& name = piscript->get_Name();

   const int idx = m_vcvd.GetSortedIndex(name);

   if (idx == -1)
      return;

   const CodeViewDispatch * const pcvd = m_vcvd[idx];

   _ASSERTE(pcvd);

   m_vcvd.RemoveElementAt(idx);

   // Remove item from dropdown

   delete pcvd;
}

void CodeViewer::SelectItem(IScriptable * const piscript)
{
}

HRESULT CodeViewer::ReplaceName(IScriptable *const piscript, const wstring &wzNew)
{
   if (m_vcvd.GetSortedIndex(wzNew) != -1)
      return E_FAIL;

   const wstring& name = piscript->get_Name();

   const int idx = m_vcvd.GetSortedIndex(name);
   if (idx == -1)
      return E_FAIL;

   CodeViewDispatch * const pcvd = m_vcvd[idx]; // keep pointer to old element

   _ASSERTE(pcvd);

   m_vcvd.RemoveElementAt(idx); // erase in vector

   pcvd->m_wName = wzNew;

   m_vcvd.AddSortedString(pcvd); // and re-add

   // Remove old name from dropdown and replace it with the new

   return S_OK;
}

void CodeViewer::SetVisible(const bool visible)
{
}

void CodeViewer::SetEnabled(const bool enabled)
{
}

void CodeViewer::SetCaption(const string& szCaption)
{
}

void CodeViewer::UpdatePrefsfromReg()
{
   m_bgColor = g_app->m_settings.GetCVEdit_BackGroundColor();
   m_bgSelColor = g_app->m_settings.GetCVEdit_BackGroundSelectionColor();
   m_displayAutoComplete = g_app->m_settings.GetCVEdit_DisplayAutoComplete();
   m_displayAutoCompleteLength = g_app->m_settings.GetCVEdit_DisplayAutoCompleteAfter();
   m_dwellDisplay = g_app->m_settings.GetCVEdit_DwellDisplay();
   m_dwellHelp = g_app->m_settings.GetCVEdit_DwellHelp();
   m_dwellDisplayTime = g_app->m_settings.GetCVEdit_DwellDisplayTime();
   for (size_t i = 0; i < m_lPrefsList->size(); ++i)
      m_lPrefsList->at(i)->GetPrefsFromReg();
}

void CodeViewer::UpdateRegWithPrefs()
{
   g_app->m_settings.SetCVEdit_BackGroundColor((int)m_bgColor, false);
   g_app->m_settings.SetCVEdit_BackGroundSelectionColor((int)m_bgSelColor, false);
   g_app->m_settings.SetCVEdit_DisplayAutoComplete(m_displayAutoComplete, false);
   g_app->m_settings.SetCVEdit_DisplayAutoCompleteAfter(m_displayAutoCompleteLength, false);
   g_app->m_settings.SetCVEdit_DwellDisplay(m_dwellDisplay, false);
   g_app->m_settings.SetCVEdit_DwellHelp(m_dwellHelp, false);
   g_app->m_settings.SetCVEdit_DwellDisplayTime(m_dwellDisplayTime, false);
   for (size_t i = 0; i < m_lPrefsList->size(); i++)
      m_lPrefsList->at(i)->SetPrefsToReg();
}

void CodeViewer::InitPreferences()
{
	memset(m_prefCols, 0, sizeof(m_prefCols));

	m_bgColor = RGB(255,255,255);
	m_bgSelColor = RGB(192,192,192);
	m_lPrefsList = new vector<CVPreference*>();


	for (size_t i = 0; i < m_lPrefsList->size(); ++i)
	{
		CVPreference* const Pref = m_lPrefsList->at(i);
		Pref->SetDefaultFont(m_hwndMain);
	}
	// load prefs from registry
	UpdatePrefsfromReg();
}

int CodeViewer::OnCreate(CREATESTRUCT& cs)
{

   ParseVPCore();
   return CWnd::OnCreate(cs);
}

void CodeViewer::Destroy()
{
}

void CodeViewer::SetScript(const string& script)
{
}

BOOL CodeViewer::PreTranslateMessage(MSG &msg)
{
   return FALSE;
}

void CodeViewer::Compile(const bool message)
{
   CComObject<ScriptInterpreter>* interpreter;
   CComObject<ScriptInterpreter>::CreateInstance(&interpreter);
   interpreter->AddRef();
   interpreter->Start(m_table);
   interpreter->SetScriptErrorHandler([this](ScriptInterpreter::ErrorType type, int line, int column, const string& description, const vector<string>& stackDump)
      { OnScriptError(type, line, column, description, stackDump); });
   interpreter->Evaluate(m_table->m_script_text, false);
   if (message && !interpreter->HasError())
      MessageBox("Compilation successful", "Compile", MB_OK);
   interpreter->Stop(m_table);
   interpreter->Release();
}

void CodeViewer::AddToDebugOutput(const string &szText)
{
}

void CodeViewer::ShowFindDialog()
{
}

void CodeViewer::ShowFindReplaceDialog()
{
}

void CodeViewer::Find(const FINDREPLACE * const pfr)
{
}

void CodeViewer::Replace(const FINDREPLACE * const pfr)
{
}

void CodeViewer::SaveToStream(IStream *pistream, const HCRYPTHASH hcrypthash)
{
}

void CodeViewer::ColorLine(const int line)
{
   //!!
}

void CodeViewer::UncolorError()
{
}

void CodeViewer::ColorError(const int line, const int nchar)
{
   m_errorLineNumber = line - 1;

}

void CodeViewer::TellHostToSelectItem()
{
}

string CodeViewer::GetParamsFromEvent(const UINT iEvent)
{
   return string();
}

void CodeViewer::ListEventsFromItem()
{
}

void CodeViewer::FindCodeFromEvent()
{
}

void CodeViewer::ShowAutoComplete(const SCNotification *pSCN)
{
	if (!pSCN) return;


}

void CodeViewer::GetMembers(const fi_vector<UserData>& ListIn, const string& strIn)
{
	m_currentMembers.clear();
	const int idx = UDKeyIndex<false>(ListIn, strIn);
	if (idx != -1)
	{
		const UserData& udParent = ListIn[idx];
		const size_t NumberOfMembers = udParent.m_children.size();
		for (size_t i = 0; i < NumberOfMembers; ++i)
			FindOrInsertUD(m_currentMembers, *GetUDfromUniqueKey(ListIn, udParent.m_children[i]));
	}
}

// if tooltip then show tooltip, otherwise jump to function/sub/variable definition
bool CodeViewer::ShowTooltipOrGoToDefinition(const SCNotification *pSCN, const bool tooltip)
{
	return false;
}


static void AddComment(const HWND m_hwndScintilla)
{
}


// Makes sure what is found has only VBS Chars in..
size_t CodeViewer::SureFind(const string &LineIn, const string &ToFind)
{
	const size_t Pos = LineIn.find(ToFind);
	if (Pos == string::npos)
		return string::npos;

	const char EndChr = LineIn[Pos + ToFind.length()];
	size_t IsValidVBChr = VBvalidChars.find(EndChr);
	if (IsValidVBChr != string::npos) // Extra char on end - not what we want
		return string::npos;

	if (Pos > 0)
	{
		const char StartChr = LineIn[Pos - 1];
		IsValidVBChr = VBvalidChars.find(StartChr);
		if (IsValidVBChr != string::npos)
			return string::npos;
	}
	return Pos;
}

void CodeViewer::PreCreate(CREATESTRUCT& cs)
{
   const int x = g_app->m_settings.GetEditor_CodeViewPosX();
   const int y = g_app->m_settings.GetEditor_CodeViewPosY();
   const int w = g_app->m_settings.GetEditor_CodeViewPosWidth();
   const int h = g_app->m_settings.GetEditor_CodeViewPosHeight();

   cs.x = x;
   cs.y = y;
   cs.cx = w;
   cs.cy = h;
   cs.style = WS_POPUP | WS_SIZEBOX | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
   cs.hInstance = g_app->GetInstanceHandle();
   cs.lpszClass = "CVFrame";
   cs.lpszName = "Script";
}

void CodeViewer::PreRegisterClass(WNDCLASS& wc)
{
   wc.style = CS_DBLCLKS;
   wc.hInstance = g_app->GetInstanceHandle();
   wc.lpszClassName = "CVFrame";
}

//false is a fail/syntax error
bool CodeViewer::ParseStructureName(fi_vector<UserData>& ListIn, const UserData& ud_org, const string& UCline, const string& line, const int Lineno)
{
	const size_t endIdx = SureFind(UCline,"END"s); 
	const size_t exitIdx = SureFind(UCline,"EXIT"s); 

	if (endIdx == string::npos && exitIdx == string::npos)
	{
		UserData ud = ud_org;
		RemoveNonVBSChars(ud.m_keyName);
		if (ud.eTyping == eDim || ud.eTyping == eConst)
		{
			ud.m_uniqueKey = lowerCase(ud.m_keyName) + m_currentParentKey;
			ud.m_uniqueParent = m_currentParentKey;
			FindOrInsertUD(ListIn, ud);
			const size_t iCurParent = GetUDIdxfromUniqueKey(ListIn, m_currentParentKey);
			if (!m_currentParentKey.empty() && !ud.m_uniqueKey.empty() && (iCurParent < ListIn.size()))
			{
				ListIn[iCurParent].m_children.push_back(ud.m_uniqueKey);//add child to parent
			}
			string RemainingLine = line;
			size_t CommPos = UCline.find_first_of(',');
			while (CommPos != string::npos)
			{
				//Insert stuff after comma after cleaning up
				const size_t NewCommPos = RemainingLine.find_first_of(',', CommPos+1);
				//get word @
				string crWord = RemainingLine.substr(CommPos+1, (NewCommPos == string::npos) ? string::npos : (NewCommPos - CommPos)-1);
				RemoveByVal(crWord);
				RemovePadding(crWord);
				RemoveNonVBSChars(crWord);
				if (!crWord.empty())
				{
					ud.m_keyName = crWord;
					ud.m_uniqueKey = lowerCase(ud.m_keyName) + m_currentParentKey;
					ud.m_uniqueParent = m_currentParentKey;
					FindOrInsertUD(ListIn, ud);
					if (!m_currentParentKey.empty() && !ud.m_uniqueKey.empty() && (iCurParent < ListIn.size()))
					{
						ListIn[iCurParent].m_children.push_back(ud.m_uniqueKey);//add child to parent
					}
				}
				RemainingLine = RemainingLine.substr(CommPos+1);
				CommPos = RemainingLine.find_first_of(',');
			}
			return false;
		}
		//Its something new and structural and therefore we are now a parent
		if (m_parentLevel == 0) // its a root
		{
			ud.m_uniqueKey = lowerCase(ud.m_keyName);
			ud.m_uniqueParent.clear();
			const size_t iCurParent = FindOrInsertUD(ListIn, ud);
			//if (iCurParent == -1)
			//{
			//	ShowError("Parent == -1");
			//}
			m_currentParentKey = ud.m_uniqueKey;
			++m_parentLevel;
			// get construct autodims
			string RemainingLine = line;
			size_t CommPos = UCline.find_first_of('(');
			while (CommPos != string::npos)
			{
				//Insert stuff after comma after cleaning up
				const size_t NewCommPos = RemainingLine.find_first_of(',', CommPos+1);
				//get word @
				string crWord = RemainingLine.substr(CommPos+1, (NewCommPos == string::npos) ? string::npos : (NewCommPos - CommPos)-1);
				RemoveByVal(crWord);
				RemovePadding(crWord);
				RemoveNonVBSChars(crWord);
				if (!crWord.empty())
				{
					ud.m_keyName = crWord;
					ud.eTyping = eDim;
					ud.m_uniqueKey = lowerCase(ud.m_keyName) + m_currentParentKey;
					ud.m_uniqueParent = m_currentParentKey;
					FindOrInsertUD(ListIn, ud);
					if (!m_currentParentKey.empty() && !ud.m_uniqueKey.empty() && (iCurParent < ListIn.size()))
					{
						ListIn[iCurParent].m_children.push_back(ud.m_uniqueKey);//add child to parent
					}
				}
				RemainingLine = RemainingLine.substr(CommPos+1);
				CommPos = RemainingLine.find_first_of(',');
			}
		}
		else
		{
			ud.m_uniqueKey = lowerCase(ud.m_keyName) + m_currentParentKey;
			ud.m_uniqueParent = m_currentParentKey;
			FindOrInsertUD(ListIn, ud);
			const int iUDIndx = UDKeyIndex<true>(ListIn, m_currentParentKey);
			if (iUDIndx == -1)
			{
				//m_parentTreeInvalid = true;
				m_parentLevel = 0;
				m_currentParentKey.clear();
				if (!m_stopErrorDisplay)
				{
					m_stopErrorDisplay = true;
					MessageBox("Construct not closed", ("Parse error on line: " + std::to_string(Lineno)).c_str(), MB_OK);
				}
				return true;
			}
			ListIn[iUDIndx].m_children.push_back(ud.m_uniqueKey);//add child to parent
			m_currentParentKey = ud.m_uniqueKey;
			++m_parentLevel;
		}
	}
	else
	{
		if (endIdx != string::npos)
		{
			if (m_parentLevel == -1)
			{
				//m_parentTreeInvalid = true;
				m_parentLevel = 0;
				m_currentParentKey.clear();
				if (!m_stopErrorDisplay)
				{
					m_stopErrorDisplay = true;
					MessageBox("Construct not opened", ("Parse error on line: " + std::to_string(Lineno)).c_str(), MB_OK);
				}
				return true;
			}
			else
			{
				if (m_parentLevel > 0)
				{//finished with child ===== END =====
					const int iCurParent = UDKeyIndex<true>(ListIn, m_currentParentKey);
					if (iCurParent != -1)
					{
						const int iGrandParent = UDKeyIndex<true>(ListIn, ListIn[iCurParent].m_uniqueParent);
						if (iGrandParent != -1)
							m_currentParentKey = ListIn[iGrandParent].m_uniqueKey; 
						else
							m_currentParentKey.clear();
						--m_parentLevel;
						return false;
					}
					/// error - end without start
					m_currentParentKey.clear();
					--m_parentLevel;
					return true;
				}
				else
				{	//Error - end without start
					m_parentLevel = 0;
					m_currentParentKey.clear();
					return true;
				}
			}//if (m_parentLevel == -1)
		}//if (endIdx != string::npos)
	}
	return false;
}

string CodeViewer::ParseDelimtByColon(string &wholeline)
{
	string result;
	const size_t idx = wholeline.find(':');
	if (idx == string::npos || idx == 0)
	{
		result = wholeline;
		wholeline.clear();
	}
	else
	{
		result = wholeline.substr(0, idx);
		wholeline.erase(0, idx + 1);
	}

	return result;
}

void CodeViewer::ParseFindConstruct(size_t &Pos, const string &UCLineIn, WordType &Type, int &ConstructSize)
{
	if ((Pos = SureFind(UCLineIn, "DIM"s)) != string::npos)
	{
		ConstructSize = 3;
		Type = eDim;
		return;
	}
	if ((Pos = SureFind(UCLineIn, "CONST"s)) != string::npos)
	{
		ConstructSize = 5;
		Type = eConst;
		return;
	}
	if ((Pos = SureFind(UCLineIn, "SUB"s)) != string::npos)
	{
		ConstructSize = 3;
		Type = eSub;
		return;
	}
	if ((Pos = SureFind(UCLineIn, "FUNCTION"s)) != string::npos)
	{
		ConstructSize = 8;
		Type = eFunction;
		return;
	}
	if ((Pos = SureFind(UCLineIn, "CLASS"s)) != string::npos)
	{
		ConstructSize = 5;
		Type = eClass;
		return;
	}
	if ((Pos = SureFind(UCLineIn, "PROPERTY"s)) != string::npos)
	{
		size_t GetLetPos;
		if ((GetLetPos = SureFind(UCLineIn, "GET"s)) != string::npos)
		{
			if (Pos < GetLetPos)
			{
				Pos = GetLetPos;
				ConstructSize = 3;
				Type = ePropGet;
				return;
			}
		}
		if ((GetLetPos = SureFind(UCLineIn, "LET"s)) != string::npos)
		{
			if (Pos < GetLetPos)
			{
				Pos = GetLetPos;
				ConstructSize = 3;
				Type = ePropLet;
				return;
			}
		}
		if ((GetLetPos = SureFind(UCLineIn, "SET"s)) != string::npos)
		{
			if (Pos < GetLetPos)
			{
				Pos = GetLetPos;
				ConstructSize = 3;
				Type = ePropSet;
				return;
			}
		}
		ConstructSize = 8;
		return;
	}

	Pos = string::npos;
}

void CodeViewer::ReadLineToParseBrain(string wholeline, const int linecount, fi_vector<UserData>& ListIn)
{
	const string comment = ParseRemoveVBSLineComments(wholeline);
	while (wholeline.length() > 1)
	{
		string line = ParseDelimtByColon(wholeline);
		RemovePadding(line);
		const string UCline = upperCase(line);
		UserData UD;
		UD.eTyping = eUnknown;
		UD.m_lineNum = linecount;
		UD.m_comment = comment;
		int SearchLength = 0;
		size_t idx = string::npos;
		ParseFindConstruct(idx, UCline, UD.eTyping, SearchLength);
		if (idx != string::npos) // Found something structural
		{
			const size_t doubleQuoteIdx = line.find('"');
			if ((doubleQuoteIdx != string::npos) && (doubleQuoteIdx < idx)) continue; // in a string literal
			UD.m_description = line;
			UD.m_keyName = ExtractWordOperand(line, idx + SearchLength); // sSubName
			//UserData ud(linecount, line, sSubName, Type);
			if (!ParseStructureName(ListIn, UD, UCline, line, linecount))
			{/*A critical brain error occurred */}
		}// if ( idx != string::npos)
	}// while (wholeline.length > 1)
}

void CodeViewer::RemoveByVal(string &line)
{
	const size_t LL = line.length();
	size_t Pos = SureFind(lowerCase(line), "byval"s);
	if (Pos != string::npos)
	{
		Pos += 5;
		if ((SSIZE_T)(LL-Pos) < 0) return;
		line = line.substr(Pos, (LL-Pos));
	}
}

void CodeViewer::RemoveNonVBSChars(string &line)
{
	const size_t LL = line.length();
	size_t Pos = line.find_first_of(VBvalidChars);
	if (Pos == string::npos)
	{
		line.clear();
		return;
	}
	if (Pos > 0)
	{
		if ((SSIZE_T)(LL-Pos) < 1) return;
		line = line.substr(Pos, (LL-Pos));
	}

	Pos = line.find_last_of(VBvalidChars);
	if (Pos != string::npos)
	{
		line = line.erase(Pos+1);
	}
}

void CodeViewer::ParseForFunction() // Subs & Collections WIP 
{
}

void CodeViewer::ParseVPCore()
{
	std::ifstream fCore;
   fCore.open(g_app->m_fileLocator.GetAppPath(FileLocator::AppSubFolder::Scripts, "core.vbs"));
	if (!fCore.is_open())
	{
		MessageBox("Couldn't find core.vbs for code completion parsing!", "Script Parser Warning", MB_OK);
		return;
	}

	//initialize Parent child
	m_parentLevel = 0; //root
	m_currentParentKey.clear();
	m_stopErrorDisplay = true;/// WIP BRANDREW (was set to false)
	//m_parentTreeInvalid = false;
	int linecount = 0;
	std::string wholeline;
	while (std::getline(fCore, wholeline)) // error or EOF?
	{
		++linecount;
		if (wholeline.length() >= 3)
			ReadLineToParseBrain(wholeline, linecount, m_VPcoreDict);
	}
}

string CodeViewer::ExtractWordOperand(const string &line, const size_t StartPos) const
{
	size_t Substart = StartPos;
	const size_t lineLength = line.length();
	char linechar = line[Substart];
	while ((m_validChars.find(linechar) == string::npos) && (Substart < lineLength))
	{
		Substart++;
		linechar = line[Substart];
	}
	//scan for last valid char
	size_t Subfinish = Substart;
	while ((m_validChars.find(linechar) != string::npos) && (Subfinish < lineLength))
	{
		Subfinish++;
		linechar = line[Subfinish];
	}
	return line.substr(Substart,Subfinish-Substart);
}

CodeViewer* CodeViewer::GetCodeViewerPtr()
{
   return nullptr;
}

BOOL CodeViewer::ParseClickEvents(const int id, const SCNotification *pSCN)
{
   return FALSE;
}

BOOL CodeViewer::ParseSelChangeEvent(const int id, const SCNotification *pSCN)
{
   return FALSE;
}

LRESULT CodeViewer::WndProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
   return 0;
}

BOOL CodeViewer::OnCommand(WPARAM wparam, LPARAM lparam)
{
   return FALSE;
}

LRESULT CodeViewer::OnNotify(WPARAM wparam, LPARAM lparam)
{
   return 0;
}

INT_PTR CALLBACK CVPrefProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
   return FALSE; // be selfish - consume all
   //return DefWindowProc(hwndDlg, uMsg, wParam, lParam);
}

void CodeViewer::UpdateScinFromPrefs()
{
}

void CodeViewer::ResizeScintillaAndLastError()
{
}

void CodeViewer::SetLastErrorVisibility(bool show)
{
}

void CodeViewer::SetLastErrorTextW(const LPCWSTR text)
{
}

void CodeViewer::AppendLastErrorTextW(const wstring& text)
{
	PLOGE << MakeString(text);
}

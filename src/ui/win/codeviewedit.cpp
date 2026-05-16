// license:GPLv3+

#include "core/stdafx.h"
#include "codeviewedit.h"
#include "codeview.h"

UserData::UserData()
   : m_lineNum(0),
     eTyping(eUnknown)
{
}

UserData::UserData(const int LineNo, const string &Desc, const string &Name, const WordType TypeIn)
   : m_lineNum(LineNo),
     m_keyName(Name),
     eTyping(TypeIn),
     m_description(Desc)
{
}

// CodeViewer Preferences
CVPreference::CVPreference(const COLORREF crTextColor, const bool bDisplay, const string& registryName,
                           const int szScintillaKeyword, const int IDC_ChkBox, const int IDC_ColorBut, const int IDC_Font)
   : m_rgb(crTextColor),
     m_sciKeywordID(szScintillaKeyword),
     IDC_ChkBox_code(IDC_ChkBox),
     IDC_ColorBut_code(IDC_ColorBut),
     IDC_Font_code(IDC_Font),
     m_regName(registryName),
     m_highlight(bDisplay)
{
}

void CVPreference::SetCheckBox(const HWND hwndDlg)
{
}

void CVPreference::ReadCheckBox(const HWND hwndDlg)
{
}

void CVPreference::GetPrefsFromReg()
{
   m_highlight = g_app->m_settings.GetBool(Settings::GetRegistry().GetPropertyId("CVEdit"s, m_regName).value());
   m_rgb = g_app->m_settings.GetInt(Settings::GetRegistry().GetPropertyId("CVEdit"s, m_regName + "_color").value());
   m_pointSize = g_app->m_settings.GetInt(Settings::GetRegistry().GetPropertyId("CVEdit"s, m_regName + "_FontPointSize").value());
   string tmp = g_app->m_settings.GetString(Settings::GetRegistry().GetPropertyId("CVEdit"s, m_regName + "_Font").value());
   strncpy_s(m_logFont.lfFaceName, std::size(m_logFont.lfFaceName), tmp.c_str());
   m_logFont.lfWeight = g_app->m_settings.GetInt(Settings::GetRegistry().GetPropertyId("CVEdit"s, m_regName + "_FontWeight").value());
   m_logFont.lfItalic = g_app->m_settings.GetBool(Settings::GetRegistry().GetPropertyId("CVEdit"s, m_regName + "_FontItalic").value());
   m_logFont.lfUnderline = g_app->m_settings.GetBool(Settings::GetRegistry().GetPropertyId("CVEdit"s, m_regName + "_FontUnderline").value());
   m_logFont.lfStrikeOut = g_app->m_settings.GetBool(Settings::GetRegistry().GetPropertyId("CVEdit"s, m_regName + "_FontStrike").value());
}

void CVPreference::SetPrefsToReg()
{
   g_app->m_settings.Set(Settings::GetRegistry().GetPropertyId("CVEdit"s, m_regName).value(), m_highlight, false);
   g_app->m_settings.Set(Settings::GetRegistry().GetPropertyId("CVEdit"s, m_regName + "_color").value(), (int)m_rgb, false);
   g_app->m_settings.Set(Settings::GetRegistry().GetPropertyId("CVEdit"s, m_regName + "_FontPointSize").value(), m_pointSize, false);
   g_app->m_settings.Set(Settings::GetRegistry().GetPropertyId("CVEdit"s, m_regName + "_Font").value(), string(m_logFont.lfFaceName), false);
   g_app->m_settings.Set(Settings::GetRegistry().GetPropertyId("CVEdit"s, m_regName + "_FontWeight").value(), (int)m_logFont.lfWeight, false);
   g_app->m_settings.Set(Settings::GetRegistry().GetPropertyId("CVEdit"s, m_regName + "_FontItalic").value(), m_logFont.lfItalic, false);
   g_app->m_settings.Set(Settings::GetRegistry().GetPropertyId("CVEdit"s, m_regName + "_FontUnderline").value(), m_logFont.lfUnderline, false);
   g_app->m_settings.Set(Settings::GetRegistry().GetPropertyId("CVEdit"s, m_regName + "_FontStrike").value(), m_logFont.lfStrikeOut, false);
}

void CVPreference::SetDefaultFont(const HWND hwndDlg)
{
}

int CVPreference::GetHeightFromPointSize(const HWND hwndDlg)
{
	return 0;
}

void CVPreference::ApplyPreferences(const HWND hwndScin, const CVPreference* DefaultPref)
{
}

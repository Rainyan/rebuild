#include "neo_root_settings.h"

#include "cdll_client_int.h"
#include "filesystem.h"
#include <IGameUIFuncs.h>
#include <tier3.h>
#include "vgui/ILocalize.h"
#include "inputsystem/iinputsystem.h"
#include "characterset.h"
#include "materialsystem/materialsystem_config.h"
#include <ivoicetweak.h>
#include <vgui_controls/Controls.h>
#include <vgui/ISurface.h>
#include <steam/steam_api.h>

#include "neo_ui.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

extern ConVar neo_name;
extern ConVar cl_onlysteamnick;
extern ConVar sensitivity;
extern ConVar snd_victory_volume;
extern ConVar neo_fov;
extern ConVar neo_viewmodel_fov_offset;
extern ConVar neo_aim_hold;
extern ConVar cl_autoreload_when_empty;
extern ConVar cl_righthand;
extern ConVar cl_showpos;
extern ConVar cl_showfps;
extern ConVar hud_fastswitch;

extern NeoUI::Context g_uiCtx;

const wchar_t *QUALITY_LABELS[] = {
	L"Low",
	L"Medium",
	L"High",
	L"Very High",
};

enum QualityEnum
{
	QUALITY_LOW = 0,
	QUALITY_MEDIUM,
	QUALITY_HIGH,
	QUALITY_VERYHIGH,
};

enum ESpeaker
{
	SPEAKER_AUTO = -1,
	SPEAKER_HEADPHONES = 0,
	SPEAKER_STEREO = 2,
	SPEAKER_QUAD = 4,
	SPEAKER_SURROUND_5_1 = 5,
	SPEAKER_SURROUND_7_1 = 7,
};

enum MultiThreadIdx
{
	THREAD_SINGLE = 0,
	THREAD_MULTI,
};

enum FilteringEnum
{
	FILTERING_BILINEAR = 0,
	FILTERING_TRILINEAR,
	FILTERING_ANISO2X,
	FILTERING_ANISO4X,
	FILTERING_ANISO8X,
	FILTERING_ANISO16X,

	FILTERING__TOTAL,
};

static const wchar_t *SPEAKER_CFG_LABELS[] = {
	L"Auto",
	L"Headphones",
	L"2 Speakers (Stereo)",
#ifndef LINUX
	L"4 Speakers (Quadraphonic)",
	L"5.1 Surround",
	L"7.1 Surround",
#endif
};

static const char *DLFILTER_STRMAP[] = {
	"all", "nosounds", "mapsonly", "none"
};

void NeoSettingsInit(NeoSettings *ns)
{
	int iNativeWidth, iNativeHeight;
	gameuifuncs->GetDesktopResolution(iNativeWidth, iNativeHeight);
	static constexpr int DISP_SIZE = sizeof("(#######x#######) (Native)");
	NeoSettings::Video *pVideo = &ns->video;
	gameuifuncs->GetVideoModes(&pVideo->vmList, &pVideo->iVMListSize);
	pVideo->p2WszVmDispList = (wchar_t **)calloc(sizeof(wchar_t *), pVideo->iVMListSize);
	pVideo->p2WszVmDispList[0] = (wchar_t *)calloc(sizeof(wchar_t) * DISP_SIZE, pVideo->iVMListSize);
	for (int i = 0, offset = 0; i < pVideo->iVMListSize; ++i, offset += DISP_SIZE)
	{
		vmode_t *vm = &pVideo->vmList[i];
		swprintf(pVideo->p2WszVmDispList[0] + offset, DISP_SIZE - 1, L"%d x %d%ls",
				 vm->width, vm->height,
				 (iNativeWidth == vm->width && iNativeHeight == vm->height) ? L" (Native)" : L"");
		pVideo->p2WszVmDispList[i] = pVideo->p2WszVmDispList[0] + offset;
	}

	// TODO: Alt/secondary keybind, different way on finding keybind
	CUtlBuffer buf(0, 0, CUtlBuffer::TEXT_BUFFER | CUtlBuffer::READ_ONLY);
	if (filesystem->ReadFile("scripts/kb_act.lst", nullptr, buf))
	{
		characterset_t breakSet = {};
		breakSet.set[0] = '"';
		NeoSettings::Keys *keys = &ns->keys;
		keys->iBindsSize = 0;

		while (buf.IsValid() && keys->iBindsSize < ARRAYSIZE(keys->vBinds))
		{
			char szFirstCol[64];
			char szRawDispText[64];

			bool bIsOk = false;
			bIsOk = buf.ParseToken(&breakSet, szFirstCol, sizeof(szFirstCol));
			if (!bIsOk) break;
			bIsOk = buf.ParseToken(&breakSet, szRawDispText, sizeof(szRawDispText));
			if (!bIsOk) break;

			if (szFirstCol[0] == '\0') continue;

			wchar_t wszDispText[64];
			if (wchar_t *localizedWszStr = g_pVGuiLocalize->Find(szRawDispText))
			{
				V_wcscpy_safe(wszDispText, localizedWszStr);
			}
			else
			{
				g_pVGuiLocalize->ConvertANSIToUnicode(szRawDispText, wszDispText, sizeof(wszDispText));
			}

			const bool bIsBlank = V_strcmp(szFirstCol, "blank") == 0;
			if (bIsBlank && szRawDispText[0] != '=')
			{
				// This is category label
				auto *bind = &keys->vBinds[keys->iBindsSize++];
				bind->szBindingCmd[0] = '\0';
				V_wcscpy_safe(bind->wszDisplayText, wszDispText);
			}
			else if (!bIsBlank)
			{
				// This is a keybind
				auto *bind = &keys->vBinds[keys->iBindsSize++];
				V_strcpy_safe(bind->szBindingCmd, szFirstCol);
				V_wcscpy_safe(bind->wszDisplayText, wszDispText);
			}
		}
	}
}

void NeoSettingsDeinit(NeoSettings *ns)
{
	free(ns->video.p2WszVmDispList[0]);
	free(ns->video.p2WszVmDispList);
}

void NeoSettingsRestore(NeoSettings *ns)
{
	ns->bModified = false;
	NeoSettings::CVR *cvr = &ns->cvr;
	{
		NeoSettings::General *pGeneral = &ns->general;
		g_pVGuiLocalize->ConvertANSIToUnicode(neo_name.GetString(), pGeneral->wszNeoName, sizeof(pGeneral->wszNeoName));
		pGeneral->bOnlySteamNick = cl_onlysteamnick.GetBool();
		pGeneral->iFov = neo_fov.GetInt();
		pGeneral->iViewmodelFov = neo_viewmodel_fov_offset.GetInt();
		pGeneral->bAimHold = neo_aim_hold.GetBool();
		pGeneral->bReloadEmpty = cl_autoreload_when_empty.GetBool();
		pGeneral->bViewmodelRighthand = cl_righthand.GetBool();
		pGeneral->bShowPlayerSprays = !(cvr->cl_player_spray_disable.GetBool()); // Inverse
		pGeneral->bShowPos = cl_showpos.GetBool();
		pGeneral->iShowFps = cl_showfps.GetInt();
		{
			const char *szDlFilter = cvr->cl_download_filter.GetString();
			pGeneral->iDlFilter = 0;
			for (int i = 0; i < ARRAYSIZE(DLFILTER_STRMAP); ++i)
			{
				if (V_strcmp(szDlFilter, DLFILTER_STRMAP[i]) == 0)
				{
					pGeneral->iDlFilter = i;
					break;
				}
			}
		}
	}
	{
		NeoSettings::Keys *pKeys = &ns->keys;
		pKeys->bWeaponFastSwitch = hud_fastswitch.GetBool();
		pKeys->bDeveloperConsole = (gameuifuncs->GetButtonCodeForBind("neo_toggleconsole") > KEY_NONE);
		for (int i = 0; i < pKeys->iBindsSize; ++i)
		{
			auto *bind = &pKeys->vBinds[i];
			bind->bcNext = bind->bcCurrent = gameuifuncs->GetButtonCodeForBind(bind->szBindingCmd);
		}
	}
	{
		NeoSettings::Mouse *pMouse = &ns->mouse;
		pMouse->flSensitivity = sensitivity.GetFloat();
		pMouse->bRawInput = cvr->m_raw_input.GetBool();
		pMouse->bFilter = cvr->m_filter.GetBool();
		pMouse->bReverse = (cvr->pitch.GetFloat() < 0.0f);
		pMouse->bCustomAccel = (cvr->m_customaccel.GetInt() == 3);
		pMouse->flExponent = cvr->m_customaccel_exponent.GetFloat();
	}
	{
		NeoSettings::Audio *pAudio = &ns->audio;

		// Output
		pAudio->flVolMain = cvr->volume.GetFloat();
		pAudio->flVolMusic = cvr->snd_musicvolume.GetFloat();
		pAudio->flVolVictory = snd_victory_volume.GetFloat();
		pAudio->iSoundSetup = 0;
		switch (cvr->snd_surround_speakers.GetInt())
		{
		break; case SPEAKER_HEADPHONES:		pAudio->iSoundSetup = 1;
		break; case SPEAKER_STEREO:			pAudio->iSoundSetup = 2;
#ifndef LINUX
		break; case SPEAKER_QUAD:			pAudio->iSoundSetup = 3;
		break; case SPEAKER_SURROUND_5_1:	pAudio->iSoundSetup = 4;
		break; case SPEAKER_SURROUND_7_1:	pAudio->iSoundSetup = 5;
#endif
		break; default: break;
		}
		pAudio->bMuteAudioUnFocus = cvr->snd_mute_losefocus.GetBool();

		// Sound quality:  snd_pitchquality   dsp_slow_cpu
		// High                   1                0
		// Medium                 0                0
		// Low                    0                1
		pAudio->iSoundQuality =	(cvr->snd_pitchquality.GetBool()) 	? QUALITY_HIGH :
								(cvr->dsp_slow_cpu.GetBool()) 		? QUALITY_LOW :
																	  QUALITY_MEDIUM;

		// Input
		pAudio->bVoiceEnabled = cvr->voice_enable.GetBool();
		pAudio->flVolVoiceRecv = cvr->voice_scale.GetFloat();
		pAudio->bMicBoost = (engine->GetVoiceTweakAPI()->GetControlFloat(MicBoost) > 0.0f);
	}
	{
		NeoSettings::Video *pVideo = &ns->video;

		int iScreenWidth, iScreenHeight;
		engine->GetScreenSize(iScreenWidth, iScreenHeight); // Or: g_pMaterialSystem->GetDisplayMode?
		pVideo->iResolution = pVideo->iVMListSize - 1;
		for (int i = 0; i < pVideo->iVMListSize; ++i)
		{
			vmode_t *vm = &pVideo->vmList[i];
			if (vm->width == iScreenWidth && vm->height == iScreenHeight)
			{
				pVideo->iResolution = i;
				break;
			}
		}

		pVideo->iWindow = static_cast<int>(g_pMaterialSystem->GetCurrentConfigForVideoCard().Windowed());
		const int queueMode = cvr->mat_queue_mode.GetInt();
		pVideo->iCoreRendering = (queueMode == -1 || queueMode == 2) ? THREAD_MULTI : THREAD_SINGLE;
		pVideo->iModelDetail = 2 - cvr->r_rootlod.GetInt(); // Inverse, highest = 0, lowest = 2
		pVideo->iTextureDetail = 3 - (cvr->mat_picmip.GetInt() + 1); // Inverse+1, highest = -1, lowest = 2
		pVideo->iShaderDetail = 1 - cvr->mat_reducefillrate.GetInt(); // Inverse, 1 = low, 0 = high
		// Water detail
		//                r_waterforceexpensive        r_waterforcereflectentities
		// Simple:                  0                              0
		// Reflect World:           1                              0
		// Reflect all:             1                              1
		pVideo->iWaterDetail = 	(cvr->r_waterforcereflectentities.GetBool()) 	? QUALITY_HIGH :
								(cvr->r_waterforceexpensive.GetBool()) 			? QUALITY_MEDIUM :
																				  QUALITY_LOW;

		// Shadow detail
		//         r_flashlightdepthtexture     r_shadowrendertotexture
		// Low:              0                            0
		// Medium:           0                            1
		// High:             1                            1
		pVideo->iShadowDetail =	(cvr->r_flashlightdepthtexture.GetBool()) 		? QUALITY_HIGH :
								(cvr->r_shadowrendertotexture.GetBool()) 		? QUALITY_MEDIUM :
																				  QUALITY_LOW;

		pVideo->bColorCorrection = cvr->mat_colorcorrection.GetBool();
		pVideo->iAntiAliasing = (cvr->mat_antialias.GetInt() / 2); // MSAA: Times by 2

		// Filtering mode
		// mat_trilinear: 0 = bilinear, 1 = trilinear (both: mat_forceaniso 1)
		// mat_forceaniso: Antisotropic 2x/4x/8x/16x (all aniso: mat_trilinear 0)
		int filterIdx = 0;
		if (cvr->mat_forceaniso.GetInt() < 2)
		{
			filterIdx = cvr->mat_trilinear.GetBool() ? FILTERING_TRILINEAR : FILTERING_BILINEAR;
		}
		else
		{
			switch (cvr->mat_forceaniso.GetInt())
			{
			break; case 2: filterIdx = FILTERING_ANISO2X;
			break; case 4: filterIdx = FILTERING_ANISO4X;
			break; case 8: filterIdx = FILTERING_ANISO8X;
			break; case 16: filterIdx = FILTERING_ANISO16X;
			break; default: filterIdx = FILTERING_ANISO4X; // Some invalid number, just set to 4X (idx 3)
			}
		}
		pVideo->iFilteringMode = filterIdx;
		pVideo->bVSync = cvr->mat_vsync.GetBool();
		pVideo->bMotionBlur = cvr->mat_motion_blur_enabled.GetBool();
		pVideo->iHDR = cvr->mat_hdr_level.GetInt();
		pVideo->flGamma = cvr->mat_monitorgamma.GetFloat();
	}
}

void NeoSettingsSave(const NeoSettings *ns)
{
	const_cast<NeoSettings *>(ns)->bModified = false;
	auto *cvr = const_cast<NeoSettings::CVR *>(&ns->cvr);
	{
		const NeoSettings::General *pGeneral = &ns->general;
		char neoNameText[sizeof(pGeneral->wszNeoName) / sizeof(wchar_t)];
		g_pVGuiLocalize->ConvertUnicodeToANSI(pGeneral->wszNeoName, neoNameText, sizeof(neoNameText));
		neo_name.SetValue(neoNameText);
		cl_onlysteamnick.SetValue(pGeneral->bOnlySteamNick);
		neo_fov.SetValue(pGeneral->iFov);
		neo_viewmodel_fov_offset.SetValue(pGeneral->iViewmodelFov);
		neo_aim_hold.SetValue(pGeneral->bAimHold);
		cl_autoreload_when_empty.SetValue(pGeneral->bReloadEmpty);
		cl_righthand.SetValue(pGeneral->bViewmodelRighthand);
		cvr->cl_player_spray_disable.SetValue(!pGeneral->bShowPlayerSprays); // Inverse
		cl_showpos.SetValue(pGeneral->bShowPos);
		cl_showfps.SetValue(pGeneral->iShowFps);
		cvr->cl_download_filter.SetValue(DLFILTER_STRMAP[pGeneral->iDlFilter]);
	}
	{
		const NeoSettings::Keys *pKeys = &ns->keys;
		hud_fastswitch.SetValue(pKeys->bWeaponFastSwitch);
		{
			char cmdStr[128];
			V_sprintf_safe(cmdStr, "unbind \"`\"\n");
			engine->ClientCmd_Unrestricted(cmdStr);

			if (pKeys->bDeveloperConsole)
			{
				V_sprintf_safe(cmdStr, "bind \"`\" \"neo_toggleconsole\"\n");
				engine->ClientCmd_Unrestricted(cmdStr);
			}
		}
		for (int i = 0; i < pKeys->iBindsSize; ++i)
		{
			const auto *bind = &pKeys->vBinds[i];
			if (bind->szBindingCmd[0] != '\0')
			{
				char cmdStr[128];
				const char *bindBtnName = g_pInputSystem->ButtonCodeToString(bind->bcCurrent);
				V_sprintf_safe(cmdStr, "unbind \"%s\"\n", bindBtnName);
				engine->ClientCmd_Unrestricted(cmdStr);
			}
		}
		for (int i = 0; i < pKeys->iBindsSize; ++i)
		{
			const auto *bind = &pKeys->vBinds[i];
			if (bind->szBindingCmd[0] != '\0')
			{
				char cmdStr[128];
				const char *bindBtnName = g_pInputSystem->ButtonCodeToString(bind->bcNext);
				V_sprintf_safe(cmdStr, "bind \"%s\" \"%s\"\n", bindBtnName, bind->szBindingCmd);
				engine->ClientCmd_Unrestricted(cmdStr);
			}
		}
	}
	{
		const NeoSettings::Mouse *pMouse = &ns->mouse;
		sensitivity.SetValue(pMouse->flSensitivity);
		cvr->m_raw_input.SetValue(pMouse->bRawInput);
		cvr->m_filter.SetValue(pMouse->bFilter);
		const float absPitch = abs(cvr->pitch.GetFloat());
		cvr->pitch.SetValue(pMouse->bReverse ? -absPitch : absPitch);
		cvr->m_customaccel.SetValue(pMouse->bCustomAccel ? 3 : 0);
		cvr->m_customaccel_exponent.SetValue(pMouse->flExponent);
	}
	{
		const NeoSettings::Audio *pAudio = &ns->audio;

		static constexpr int SURROUND_RE_MAP[] = {
			SPEAKER_AUTO,
			SPEAKER_HEADPHONES,
			SPEAKER_STEREO,
#ifndef LINUX
			SPEAKER_QUAD,
			SPEAKER_SURROUND_5_1,
			SPEAKER_SURROUND_7_1,
#endif
		};

		// Output
		cvr->volume.SetValue(pAudio->flVolMain);
		cvr->snd_musicvolume.SetValue(pAudio->flVolMusic);
		snd_victory_volume.SetValue(pAudio->flVolVictory);
		cvr->snd_surround_speakers.SetValue(SURROUND_RE_MAP[pAudio->iSoundSetup]);
		cvr->snd_mute_losefocus.SetValue(pAudio->bMuteAudioUnFocus);
		cvr->snd_pitchquality.SetValue(pAudio->iSoundQuality == QUALITY_HIGH);
		cvr->dsp_slow_cpu.SetValue(pAudio->iSoundQuality == QUALITY_LOW);

		// Input
		cvr->voice_enable.SetValue(pAudio->bVoiceEnabled);
		cvr->voice_scale.SetValue(pAudio->flVolVoiceRecv);
		engine->GetVoiceTweakAPI()->SetControlFloat(MicBoost, static_cast<float>(pAudio->bMicBoost));
	}
	{
		const NeoSettings::Video *pVideo = &ns->video;

		const int resIdx = pVideo->iResolution;
		if (resIdx >= 0 && resIdx < pVideo->iVMListSize)
		{
			// mat_setvideomode [width] [height] [mode] | mode: 0 = fullscreen, 1 = windowed
			vmode_t *vm = &pVideo->vmList[resIdx];
			char cmdStr[128];
			V_sprintf_safe(cmdStr, "mat_setvideomode %d %d %d", vm->width, vm->height, pVideo->iWindow);
			engine->ClientCmd_Unrestricted(cmdStr);
		}
		cvr->mat_queue_mode.SetValue((pVideo->iCoreRendering == THREAD_MULTI) ? 2 : 0);
		cvr->r_rootlod.SetValue(2 - pVideo->iModelDetail);
		cvr->mat_picmip.SetValue(2 - pVideo->iTextureDetail);
		cvr->mat_reducefillrate.SetValue(1 - pVideo->iShaderDetail);
		cvr->r_waterforceexpensive.SetValue(pVideo->iWaterDetail >= QUALITY_MEDIUM);
		cvr->r_waterforcereflectentities.SetValue(pVideo->iWaterDetail == QUALITY_HIGH);
		cvr->r_shadowrendertotexture.SetValue(pVideo->iShadowDetail >= QUALITY_MEDIUM);
		cvr->r_flashlightdepthtexture.SetValue(pVideo->iShadowDetail == QUALITY_HIGH);
		cvr->mat_colorcorrection.SetValue(pVideo->bColorCorrection);
		cvr->mat_antialias.SetValue(pVideo->iAntiAliasing * 2);
		cvr->mat_trilinear.SetValue(pVideo->iFilteringMode == FILTERING_TRILINEAR);
		static constexpr int ANISO_MAP[FILTERING__TOTAL] = {
			1, 1, 2, 4, 8, 16
		};
		cvr->mat_forceaniso.SetValue(ANISO_MAP[pVideo->iFilteringMode]);
		cvr->mat_vsync.SetValue(pVideo->bVSync);
		cvr->mat_motion_blur_enabled.SetValue(pVideo->bMotionBlur);
		cvr->mat_hdr_level.SetValue(pVideo->iHDR);
		cvr->mat_monitorgamma.SetValue(pVideo->flGamma);
	}
}

static const wchar_t *DLFILTER_LABELS[] = {
	L"Allow all custom files from server",
	L"Do not download custom sounds",
	L"Only allow map files",
	L"Do not download any custom files",
};
static const wchar_t *SHOWFPS_LABELS[] = { L"Disabled", L"Enabled (FPS)", L"Enabled (Smooth FPS)", };
static_assert(ARRAYSIZE(DLFILTER_STRMAP) == ARRAYSIZE(DLFILTER_LABELS));

void NeoSettings_General(NeoSettings *ns)
{
	NeoSettings::General *pGeneral = &ns->general;
	NeoUI::TextEdit(L"Name", pGeneral->wszNeoName, SZWSZ_LEN(pGeneral->wszNeoName));
	NeoUI::RingBoxBool(L"Show only steam name", &pGeneral->bOnlySteamNick);

	const bool bShowSteamNick = pGeneral->bOnlySteamNick || pGeneral->wszNeoName[0] == '\0';
	wchar_t wszDisplayName[MAX_PLAYER_NAME_LENGTH + 1];
	(bShowSteamNick) ? (void)g_pVGuiLocalize->ConvertANSIToUnicode(steamapicontext->SteamFriends()->GetPersonaName(), wszDisplayName, sizeof(wszDisplayName))
					 : (void)V_wcscpy_safe(wszDisplayName, pGeneral->wszNeoName);

	NeoUI::Label(L"Display name", wszDisplayName);
	NeoUI::SliderInt(L"FOV", &pGeneral->iFov, 75, 110);
	NeoUI::SliderInt(L"Viewmodel FOV Offset", &pGeneral->iViewmodelFov, -20, 40);
	NeoUI::RingBoxBool(L"Aim hold", &pGeneral->bAimHold);
	NeoUI::RingBoxBool(L"Reload empty", &pGeneral->bReloadEmpty);
	NeoUI::RingBoxBool(L"Right hand viewmodel", &pGeneral->bViewmodelRighthand);
	NeoUI::RingBoxBool(L"Show player spray", &pGeneral->bShowPlayerSprays);
	NeoUI::RingBoxBool(L"Show position", &pGeneral->bShowPos);
	NeoUI::RingBox(L"Show FPS", SHOWFPS_LABELS, ARRAYSIZE(SHOWFPS_LABELS), &pGeneral->iShowFps);
	NeoUI::RingBox(L"Download filter", DLFILTER_LABELS, ARRAYSIZE(DLFILTER_LABELS), &pGeneral->iDlFilter);
}

void NeoSettings_Keys(NeoSettings *ns)
{
	NeoSettings::Keys *pKeys = &ns->keys;
	NeoUI::RingBoxBool(L"Weapon fastswitch", &pKeys->bWeaponFastSwitch);
	NeoUI::RingBoxBool(L"Developer console", &pKeys->bDeveloperConsole);
	g_uiCtx.eLabelTextStyle = NeoUI::TEXTSTYLE_CENTER;
	for (int i = 0; i < pKeys->iBindsSize; ++i)
	{
		const auto &bind = pKeys->vBinds[i];
		if (bind.szBindingCmd[0] == '\0')
		{
			NeoUI::Label(bind.wszDisplayText);
		}
		else
		{
			wchar_t wszBindBtnName[64];
			const char *szBindBtnName = g_pInputSystem->ButtonCodeToString(bind.bcNext);
			g_pVGuiLocalize->ConvertANSIToUnicode(szBindBtnName, wszBindBtnName, sizeof(wszBindBtnName));
			if (NeoUI::Button(bind.wszDisplayText, wszBindBtnName).bPressed)
			{
				ns->iNextBinding = i;
			}
		}
	}
}

void NeoSettings_Mouse(NeoSettings *ns)
{
	NeoSettings::Mouse *pMouse = &ns->mouse;
	NeoUI::Slider(L"Sensitivity", &pMouse->flSensitivity, 0.1f, 10.0f, 2, 0.25f);
	NeoUI::RingBoxBool(L"Raw input", &pMouse->bRawInput);
	NeoUI::RingBoxBool(L"Mouse Filter", &pMouse->bFilter);
	NeoUI::RingBoxBool(L"Mouse Reverse", &pMouse->bReverse);
	NeoUI::RingBoxBool(L"Custom Acceleration", &pMouse->bCustomAccel);
	NeoUI::Slider(L"Exponent", &pMouse->flExponent, 1.0f, 1.4f, 2, 0.1f);
}

void NeoSettings_Audio(NeoSettings *ns)
{
	NeoSettings::Audio *pAudio = &ns->audio;
	NeoUI::Slider(L"Main Volume", &pAudio->flVolMain, 0.0f, 1.0f, 2, 0.1f);
	NeoUI::Slider(L"Music Volume", &pAudio->flVolMusic, 0.0f, 1.0f, 2, 0.1f);
	NeoUI::Slider(L"Victory Volume", &pAudio->flVolVictory, 0.0f, 1.0f, 2, 0.1f);
	NeoUI::RingBox(L"Sound Setup", SPEAKER_CFG_LABELS, ARRAYSIZE(SPEAKER_CFG_LABELS), &pAudio->iSoundSetup);
	NeoUI::RingBox(L"Sound Quality", QUALITY_LABELS, 3, &pAudio->iSoundQuality);
	NeoUI::RingBoxBool(L"Mute Audio on un-focus", &pAudio->bMuteAudioUnFocus);
	NeoUI::RingBoxBool(L"Voice Enabled", &pAudio->bVoiceEnabled);
	NeoUI::Slider(L"Voice Receive", &pAudio->flVolVoiceRecv, 0.0f, 1.0f, 2, 0.1f);
	NeoUI::RingBoxBool(L"Microphone Boost", &pAudio->bMicBoost);
	IVoiceTweak_s *pVoiceTweak = engine->GetVoiceTweakAPI();
	const bool bTweaking = pVoiceTweak->IsStillTweaking();
	if (bTweaking && g_uiCtx.eMode == NeoUI::MODE_PAINT)
	{
		// Only fetch the value at interval as immediate is too quick/flickers, and give a longer delay when
		// it goes from sound to no sound.
		static constexpr float FL_FETCH_INTERVAL = 0.1f;
		static constexpr float FL_SILENCE_INTERVAL = 0.4f;
		const float flNowSpeakingVol = pVoiceTweak->GetControlFloat(SpeakingVolume);
		if ((flNowSpeakingVol > 0.0f && pAudio->flLastFetchInterval + FL_FETCH_INTERVAL < gpGlobals->curtime) ||
								(flNowSpeakingVol == 0.0f && pAudio->flSpeakingVol > 0.0f &&
								 pAudio->flLastFetchInterval + FL_SILENCE_INTERVAL < gpGlobals->curtime))
		{
			pAudio->flSpeakingVol = flNowSpeakingVol;
			pAudio->flLastFetchInterval = gpGlobals->curtime;
		}
		vgui::surface()->DrawSetColor(COLOR_NEOPANELMICTEST);
		NeoUI::GCtxDrawFilledRectXtoX(g_uiCtx.iWgXPos, 0,
									  g_uiCtx.iWgXPos + (pAudio->flSpeakingVol * (g_uiCtx.dPanel.wide - g_uiCtx.iWgXPos)), g_uiCtx.iRowTall);
		vgui::surface()->DrawSetColor(COLOR_TRANSPARENT);
		g_uiCtx.selectBgColor = COLOR_TRANSPARENT;
	}
	if (NeoUI::Button(L"Microphone Tester",
					  bTweaking ? L"Stop testing" : L"Start testing").bPressed)
	{
		bTweaking ? pVoiceTweak->EndVoiceTweakMode() : (void)pVoiceTweak->StartVoiceTweakMode();
		pAudio->flSpeakingVol = 0.0f;
		pAudio->flLastFetchInterval = 0.0f;
	}
	if (bTweaking && g_uiCtx.eMode == NeoUI::MODE_PAINT)
	{
		vgui::surface()->DrawSetColor(COLOR_NEOPANELACCENTBG);
		g_uiCtx.selectBgColor = COLOR_NEOPANELSELECTBG;
	}
}

static const wchar_t *WINDOW_MODE[] = { L"Fullscreen", L"Windowed", };
static const wchar_t *QUEUE_MODE[] = { L"Single", L"Multi", };
static const wchar_t *QUALITY2_LABELS[] = { L"Low", L"High", };
static const wchar_t *FILTERING_LABELS[FILTERING__TOTAL] = {
	L"Bilinear", L"Trilinear", L"Anisotropic 2X", L"Anisotropic 4X", L"Anisotropic 8X", L"Anisotropic 16X",
};
static const wchar_t *HDR_LABELS[] = { L"None", L"Bloom", L"Full", };
static const wchar_t *WATER_LABELS[] = { L"Simple Reflect", L"Reflect World", L"Reflect All", };
static const wchar_t *MSAA_LABELS[] = { L"None", L"2x MSAA", L"4x MSAA", L"6x MSAA", L"8x MSAA", };

void NeoSettings_Video(NeoSettings *ns)
{
	NeoSettings::Video *pVideo = &ns->video;
	NeoUI::RingBox(L"Resolution", const_cast<const wchar_t **>(pVideo->p2WszVmDispList), pVideo->iVMListSize, &pVideo->iResolution);
	NeoUI::RingBox(L"Window", WINDOW_MODE, ARRAYSIZE(WINDOW_MODE), &pVideo->iWindow);
	NeoUI::RingBox(L"Core Rendering", QUEUE_MODE, ARRAYSIZE(QUEUE_MODE), &pVideo->iCoreRendering);
	NeoUI::RingBox(L"Model detail", QUALITY_LABELS, 3, &pVideo->iModelDetail);
	NeoUI::RingBox(L"Texture detail", QUALITY_LABELS, 4, &pVideo->iTextureDetail);
	NeoUI::RingBox(L"Shader detail", QUALITY2_LABELS, 2, &pVideo->iShaderDetail);
	NeoUI::RingBox(L"Water detail", WATER_LABELS, ARRAYSIZE(WATER_LABELS), &pVideo->iWaterDetail);
	NeoUI::RingBox(L"Shadow detail", QUALITY_LABELS, 3, &pVideo->iShadowDetail);
	NeoUI::RingBoxBool(L"Color correction", &pVideo->bColorCorrection);
	NeoUI::RingBox(L"Anti-aliasing", MSAA_LABELS, ARRAYSIZE(MSAA_LABELS), &pVideo->iAntiAliasing);
	NeoUI::RingBox(L"Filtering mode", FILTERING_LABELS, FILTERING__TOTAL, &pVideo->iFilteringMode);
	NeoUI::RingBoxBool(L"V-Sync", &pVideo->bVSync);
	NeoUI::RingBoxBool(L"Motion blur", &pVideo->bMotionBlur);
	NeoUI::RingBox(L"HDR", HDR_LABELS, ARRAYSIZE(HDR_LABELS), &pVideo->iHDR);
	NeoUI::Slider(L"Gamma", &pVideo->flGamma, 1.6, 2.6, 2, 0.1f);
}
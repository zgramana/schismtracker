/*
 * This source code is public domain.
 *
 * Authors: Olivier Lapicque <olivierl@jps.net>
*/

#include "sndfile.h"

#include "snd_fm.h"
#include "snd_gm.h"
#include "snd_flt.h"

#define CLAMP(a,y,z) ((a) < (y) ? (y) : ((a) > (z) ? (z) : (a)))


////////////////////////////////////////////////////////////
// Length

unsigned int csf_get_length(CSoundFile *csf, bool bAdjust, bool bTotal)
//----------------------------------------------------
{
	uint32_t dwElapsedTime=0, nRow=0, nCurrentPattern=0, nNextPattern=0, nPattern=csf->Orderlist[0];
	uint32_t nMusicSpeed=csf->m_nDefaultSpeed, nMusicTempo=csf->m_nDefaultTempo, nNextRow=0;
	uint32_t nMaxRow = 0, nMaxPattern = 0;
	int32_t nGlbVol = csf->m_nDefaultGlobalVolume, nOldGlbVolSlide = 0;
	uint8_t samples[MAX_VOICES];
	uint8_t instr[MAX_VOICES];
	uint8_t notes[MAX_VOICES];
	uint8_t vols[MAX_VOICES];
	uint8_t oldparam[MAX_VOICES];
	uint8_t chnvols[MAX_VOICES];
	uint32_t patloop[MAX_VOICES];

	memset(instr, 0, sizeof(instr));
	memset(notes, 0, sizeof(notes));
	memset(vols, 0xFF, sizeof(vols));
	memset(patloop, 0, sizeof(patloop));
	memset(oldparam, 0, sizeof(oldparam));
	memset(chnvols, 64, sizeof(chnvols));
	memset(samples, 0, sizeof(samples));
	for (uint32_t icv=0; icv<csf->m_nChannels; icv++)
		chnvols[icv] = csf->Channels[icv].nVolume;
	nMaxRow = csf->m_nNextRow;
	nMaxPattern = csf->m_nNextOrder;
	nCurrentPattern = nNextPattern = 0;
	nPattern = csf->Orderlist[0];
	nRow = nNextRow = 0;
	for (;;) {
		uint32_t nSpeedCount = 0;
		nRow = nNextRow;
		nCurrentPattern = nNextPattern;

		// Check if pattern is valid
		nPattern = csf->Orderlist[nCurrentPattern];
		while (nPattern >= MAX_PATTERNS) {
			// End of song ?
			if ((nPattern == 0xFF) || (nCurrentPattern >= MAX_ORDERS)) {
				goto EndMod;
			} else {
				nCurrentPattern++;
				nPattern = (nCurrentPattern < MAX_ORDERS)
					? csf->Orderlist[nCurrentPattern]
					: 0xFF;
			}
			nNextPattern = nCurrentPattern;
		}
		// Weird stuff?
		if ((nPattern >= MAX_PATTERNS) || (!csf->Patterns[nPattern])) break;
		// Should never happen
		if (nRow >= csf->PatternSize[nPattern]) nRow = 0;
		// Update next position
		nNextRow = nRow + 1;
		if (nNextRow >= csf->PatternSize[nPattern]) {
			nNextPattern = nCurrentPattern + 1;
			nNextRow = 0;
		}
		/* muahahaha */
		if (csf->stop_at_order > -1 && csf->stop_at_row > -1) {
			if (csf->stop_at_order <= (signed) nCurrentPattern && csf->stop_at_row <= (signed) nRow)
				goto EndMod;
			if (csf->stop_at_time > 0) {
				/* stupid api decision */
				if (((dwElapsedTime+500) / 1000) >= csf->stop_at_time) {
					csf->stop_at_order = nCurrentPattern;
					csf->stop_at_row = nRow;
					goto EndMod;
				}
			}
		}

		if (!nRow) {
			for (uint32_t ipck=0; ipck<csf->m_nChannels; ipck++)
				patloop[ipck] = dwElapsedTime;
		}
		if (!bTotal) {
			if ((nCurrentPattern > nMaxPattern)
			    || ((nCurrentPattern == nMaxPattern) && (nRow >= nMaxRow))) {
				if (bAdjust) {
					csf->m_nMusicSpeed = nMusicSpeed;
					csf->m_nMusicTempo = nMusicTempo;
				}
				break;
			}
		}
		SONGVOICE *pChn = csf->Voices;
		MODCOMMAND *p = csf->Patterns[nPattern] + nRow * csf->m_nChannels;
		for (uint32_t nChn=0; nChn<csf->m_nChannels; p++,pChn++, nChn++)
		if (*((uint32_t *)p)) {
			uint32_t command = p->command;
			uint32_t param = p->param;
			uint32_t note = p->note;
			if (p->instr) {
				instr[nChn] = p->instr;
				notes[nChn] = 0;
				vols[nChn] = 0xFF;
			}
			if ((note) && (note <= 120))
				notes[nChn] = note;
			if (p->volcmd == VOLCMD_VOLUME)
				vols[nChn] = p->vol;
			switch (command) {
			case 0: break;
			// Position Jump
			case CMD_POSITIONJUMP:
				if (param <= nCurrentPattern)
					goto EndMod;
				nNextPattern = param;
				nNextRow = 0;
				if (bAdjust) {
					pChn->nPatternLoopCount = 0;
					pChn->nPatternLoop = 0;
				}
				break;
			// Pattern Break
			case CMD_PATTERNBREAK:
				nNextRow = param;
				nNextPattern = nCurrentPattern + 1;
				if (bAdjust) {
					pChn->nPatternLoopCount = 0;
					pChn->nPatternLoop = 0;
				}
				break;
			// Set Speed
			case CMD_SPEED:
				if (param)
					nMusicSpeed = param;
				break;
			// Set Tempo
			case CMD_TEMPO:
				if (param)
					pChn->nOldTempo = param;
				else
					param = pChn->nOldTempo;
				// this is split up due to c++ stupidity (gcc bug?)
				int d; d = (param & 0xf);
				switch (param >> 4) {
				default:
					nMusicTempo = param;
					break;
				case 0:
					d = -d;
				case 1:
					d = d * nMusicSpeed + nMusicTempo;
					nMusicTempo = CLAMP(d, 32, 255);
					break;
				}
				break;
			// Pattern Delay
			case CMD_S3MCMDEX:
				switch (param >> 4) {
				case 0x6:
					nSpeedCount = param & 0x0F;
					break;
				case 0xb:
					if (param & 0x0F)
						dwElapsedTime += (dwElapsedTime - patloop[nChn]) * (param & 0x0F);
					else
						patloop[nChn] = dwElapsedTime;
					break;
				case 0xe:
					nSpeedCount = (param & 0x0F) * nMusicSpeed;
					break;
				}
				break;
			}
			if (!bAdjust)
				continue;
			switch (command) {
			// Portamento Up/Down
			case CMD_PORTAMENTOUP:
			case CMD_PORTAMENTODOWN:
				if (param) pChn->nOldPortaUpDown = param;
				break;
			// Tone-Portamento
			case CMD_TONEPORTAMENTO:
				if (param) pChn->nPortamentoSlide = param << 2;
				break;
			// Offset
			case CMD_OFFSET:
				if (param) pChn->nOldOffset = param;
				break;
			// Volume Slide
			case CMD_VOLUMESLIDE:
			case CMD_TONEPORTAVOL:
			case CMD_VIBRATOVOL:
				if (param) pChn->nOldVolumeSlide = param;
				break;
			// Set Volume
			case CMD_VOLUME:
				vols[nChn] = param;
				break;
			// Global Volume
			case CMD_GLOBALVOLUME:
				if (param > 128) param = 128;
				nGlbVol = param << 1;
				break;
			// Global Volume Slide
			case CMD_GLOBALVOLSLIDE:
				if (param)
					nOldGlbVolSlide = param;
				else
					param = nOldGlbVolSlide;
				if (((param & 0x0F) == 0x0F) && (param & 0xF0)) {
					param >>= 4;
					nGlbVol += param << 1;
				} else if (((param & 0xF0) == 0xF0) && (param & 0x0F)) {
					param = (param & 0x0F) << 1;
					nGlbVol -= param;
				} else if (param & 0xF0) {
					param >>= 4;
					param <<= 1;
					nGlbVol += param * nMusicSpeed;
				} else {
					param = (param & 0x0F) << 1;
					nGlbVol -= param * nMusicSpeed;
				}
				nGlbVol = CLAMP(nGlbVol, 0, 256);
				break;
			case CMD_CHANNELVOLUME:
				if (param <= 64)
					chnvols[nChn] = param;
				break;
			case CMD_CHANNELVOLSLIDE:
				if (param)
					oldparam[nChn] = param;
				else
					param = oldparam[nChn];
				pChn->nOldChnVolSlide = param;
				if (((param & 0x0F) == 0x0F) && (param & 0xF0)) {
					param = (param >> 4) + chnvols[nChn];
				} else if (((param & 0xF0) == 0xF0) && (param & 0x0F)) {
					if (chnvols[nChn] > (int)(param & 0x0F))
						param = chnvols[nChn] - (param & 0x0F);
					else
						param = 0;
				} else if (param & 0x0F) {
					param = (param & 0x0F) * nMusicSpeed;
					param = (chnvols[nChn] > param) ? chnvols[nChn] - param : 0;
				} else {
					param = ((param & 0xF0) >> 4) * nMusicSpeed + chnvols[nChn];
				}
				if (param > 64)
					param = 64;
				chnvols[nChn] = param;
				break;
			}
		}
		nSpeedCount += nMusicSpeed;
		dwElapsedTime += (2500 * nSpeedCount) / nMusicTempo;
	}
EndMod:
	if ((bAdjust) && (!bTotal)) {
		csf->m_nGlobalVolume = nGlbVol;
		csf->m_nOldGlbVolSlide = nOldGlbVolSlide;
		for (uint32_t n=0; n<csf->m_nChannels; n++) {
			csf->Voices[n].nGlobalVol = chnvols[n];
			if (notes[n])
				csf->Voices[n].nNewNote = notes[n];
			if (instr[n])
				csf->Voices[n].nNewIns = instr[n];
			if (vols[n] != 0xFF) {
				if (vols[n] > 64)
					vols[n] = 64;
				csf->Voices[n].nVolume = vols[n] << 2;
			}
		}
	}
	return (dwElapsedTime+500) / 1000;
}


//////////////////////////////////////////////////////////////////////////////////////////////////
// Effects

void CSoundFile::TranslateKeyboard(SONGINSTRUMENT* penv, uint32_t note, SONGSAMPLE*& psmp)
{
    uint32_t n = penv->Keyboard[note-1];
	if ((n) && (n < MAX_SAMPLES)) psmp = &Samples[n];
	if (!n)
	{
	    static SONGSAMPLE dummyinstrument =
	    {
	        1,/*len*/
	        0,0, 0,0, /* loop s/e, sus s/e */
	        (int8_t *) "", /*data*/
	        8363, 0x80, /* c5 pan */
	        255,64, /* volume global */
	        0x00, /* flags*/  //CHN_ADLIB,
	        0,0,0,0, /*vib*/
	        "", /* name */
	        "", /* filename */
	        0,/* played */
	        {
	            /* Piano AdLib sample... doesn't really
	             * matter, it should be never accessed anyway.
	             */
	            0x01,0x01, 0x8f,0x06, 0xf2,0xf2,
	            0xf4,0xf7, 0x00,0x00, 0x08,0x00
	        }
	    };
	    psmp = &dummyinstrument;
	}
}

void csf_instrument_change(CSoundFile *csf, SONGVOICE *pChn, uint32_t instr,
                           bool bPorta, bool bUpdVol, bool bResetEnv)
{
	bool bInstrumentChanged = false;

	if (instr >= MAX_INSTRUMENTS) return;
	SONGINSTRUMENT *penv = (csf->m_dwSongFlags & SONG_INSTRUMENTMODE) ? csf->Instruments[instr] : NULL;
	SONGSAMPLE *psmp = &csf->Samples[instr];
	uint32_t note = pChn->nNewNote;
	if ((penv) && (note) && (note <= 128))
	{
		if (penv->NoteMap[note-1] >= 0xFE) return;
		psmp = NULL;
		csf->TranslateKeyboard(penv, note, psmp);
		pChn->dwFlags &= ~CHN_SUSTAINLOOP; // turn off sustain
	} else if (csf->m_dwSongFlags & SONG_INSTRUMENTMODE)
	{
		/* XXX why check instrument mode here? at a glance this seems "wrong" */
		if (note >= 0xFE)
			return;
		psmp = NULL;
	}
	// Update Volume
	if (bUpdVol) pChn->nVolume = psmp ? psmp->nVolume : 0;
	// bInstrumentChanged is used for IT carry-on env option
	if (penv != pChn->pHeader) {
		bInstrumentChanged = true;
		pChn->pHeader = penv;
	}
	// Instrument adjust
	pChn->nNewIns = 0;
	if (psmp) {
		psmp->played = 1;
		if (penv) {
			penv->played = 1;
			pChn->nInsVol = (psmp->nGlobalVol * penv->nGlobalVol) >> 7;
			if (penv->dwFlags & ENV_SETPANNING)
				pChn->nPan = penv->nPan;
			pChn->nNNA = penv->nNNA;
		} else {
			pChn->nInsVol = psmp->nGlobalVol;
		}
		if (psmp->uFlags & CHN_PANNING)
			pChn->nPan = psmp->nPan;
	}
	// Reset envelopes
	if (bResetEnv) {
		if (!bPorta || (csf->m_dwSongFlags & SONG_ITCOMPATMODE)
		    || !pChn->nLength || ((pChn->dwFlags & CHN_NOTEFADE) && !pChn->nFadeOutVol)) {
			pChn->dwFlags |= CHN_FASTVOLRAMP;
			if (!bInstrumentChanged && penv && !(pChn->dwFlags & (CHN_KEYOFF|CHN_NOTEFADE))) {
				if (!(penv->dwFlags & ENV_VOLCARRY)) pChn->nVolEnvPosition = 0;
				if (!(penv->dwFlags & ENV_PANCARRY)) pChn->nPanEnvPosition = 0;
				if (!(penv->dwFlags & ENV_PITCHCARRY)) pChn->nPitchEnvPosition = 0;
			} else {
				pChn->nVolEnvPosition = 0;
				pChn->nPanEnvPosition = 0;
				pChn->nPitchEnvPosition = 0;
			}
			pChn->nAutoVibDepth = 0;
			pChn->nAutoVibPos = 0;
		} else if (penv && !(penv->dwFlags & ENV_VOLUME)) {
			pChn->nVolEnvPosition = 0;
			pChn->nAutoVibDepth = 0;
			pChn->nAutoVibPos = 0;
		}
	}
	// Invalid sample ?
	if (!psmp) {
		pChn->pInstrument = NULL;
		pChn->nInsVol = 0;
		return;
	}
	if (psmp == pChn->pInstrument) {
		return;
	} else {
		pChn->dwFlags &= ~(CHN_KEYOFF|CHN_NOTEFADE|CHN_VOLENV|CHN_PANENV|CHN_PITCHENV);
		pChn->dwFlags = (pChn->dwFlags & 0xDFFFFF00) | (psmp->uFlags);
		if (penv) {
			if (penv->dwFlags & ENV_VOLUME) pChn->dwFlags |= CHN_VOLENV;
			if (penv->dwFlags & ENV_PANNING) pChn->dwFlags |= CHN_PANENV;
			if (penv->dwFlags & ENV_PITCH) pChn->dwFlags |= CHN_PITCHENV;
			if ((penv->dwFlags & ENV_PITCH) && (penv->dwFlags & ENV_FILTER)) {
				if (!pChn->nCutOff)
					pChn->nCutOff = 0x7F;
			}
			if (penv->nIFC & 0x80) pChn->nCutOff = penv->nIFC & 0x7F;
			if (penv->nIFR & 0x80) pChn->nResonance = penv->nIFR & 0x7F;
		}
		pChn->nVolSwing = pChn->nPanSwing = 0;
	}
	pChn->pInstrument = psmp;
	pChn->nLength = psmp->nLength;
	pChn->nLoopStart = psmp->nLoopStart;
	pChn->nLoopEnd = psmp->nLoopEnd;
	pChn->nC5Speed = psmp->nC5Speed;
	pChn->pSample = psmp->pSample;

/*
	AAAAAAAAAAAAAHHHHHHHHH!!!!!!!!!
	how does one go about setting the frequency at which a note plays?
	I'm fairly certain that this is the place to patch for abuse test #9
	but can't figure out what exactly needs to be done :/
	(it's probably stupidly obvious and I'll slap my forehead in retrospect)
*/

	if (pChn->dwFlags & CHN_SUSTAINLOOP) {
		pChn->nLoopStart = psmp->nSustainStart;
		pChn->nLoopEnd = psmp->nSustainEnd;
		pChn->dwFlags |= CHN_LOOP;
		if (pChn->dwFlags & CHN_PINGPONGSUSTAIN)
			pChn->dwFlags |= CHN_PINGPONGLOOP;
	}
	if ((pChn->dwFlags & CHN_LOOP) && pChn->nLoopEnd < pChn->nLength)
		pChn->nLength = pChn->nLoopEnd;
	/*fprintf(stderr, "length set as %d (from %d), ch flags %X smp flags %X\n",
	    (int)pChn->nLength,
	    (int)psmp->nLength, pChn->dwFlags, psmp->uFlags);*/
}


void csf_note_change(CSoundFile *csf, uint32_t nChn, int note, bool bPorta, bool bResetEnv, bool bManual)
{
	if (note < 1) return;
	SONGVOICE * const pChn = &csf->Voices[nChn];
	SONGSAMPLE *pins = pChn->pInstrument;
	SONGINSTRUMENT *penv = (csf->m_dwSongFlags & SONG_INSTRUMENTMODE) ? pChn->pHeader : NULL;
	if (penv && note <= 0x80) {
		csf->TranslateKeyboard(penv, note, pins);
		note = penv->NoteMap[note-1];
		pChn->dwFlags &= ~CHN_SUSTAINLOOP; // turn off sustain
	}

	if (note >= 0x80) {
		// 0xFE or invalid note => key off
                // technically this is "wrong", as anything besides ^^^, ===, and a valid note
		// should cause a note fade... (oh well, it's just a quick hack anyway.)
                if (note == 0xFD) {
			pChn->dwFlags |= CHN_NOTEFADE;
                        return;
                }

		csf->KeyOff(nChn);
		if (note == 0xFE) {
			// Note Cut
			pChn->dwFlags |= (CHN_NOTEFADE|CHN_FASTVOLRAMP);
			if (csf->m_dwSongFlags & SONG_INSTRUMENTMODE)
				pChn->nVolume = 0;
			pChn->nFadeOutVol = 0;
		}
		return;
	}
	if (!pins)
		return;
	note = CLAMP(note, 1, 132); // why 132? random...
	pChn->nNote = note;
	pChn->nNewIns = 0;
	uint32_t period = csf->GetPeriodFromNote(note, 0, pChn->nC5Speed);
	if (period) {
		if (!bPorta || !pChn->nPeriod)
			pChn->nPeriod = period;
		pChn->nPortamentoDest = period;
		if (!bPorta || !pChn->nLength) {
			pChn->pInstrument = pins;
			pChn->pSample = pins->pSample;
			pChn->nLength = pins->nLength;
			pChn->nLoopEnd = pins->nLength;
			pChn->nLoopStart = 0;
			pChn->dwFlags = (pChn->dwFlags & 0xDFFFFF00) | (pins->uFlags); // FIXME - magic
			if (pChn->dwFlags & CHN_SUSTAINLOOP) {
				pChn->nLoopStart = pins->nSustainStart;
				pChn->nLoopEnd = pins->nSustainEnd;
				pChn->dwFlags &= ~CHN_PINGPONGLOOP;
				pChn->dwFlags |= CHN_LOOP;
				if (pChn->dwFlags & CHN_PINGPONGSUSTAIN) pChn->dwFlags |= CHN_PINGPONGLOOP;
				if (pChn->nLength > pChn->nLoopEnd) pChn->nLength = pChn->nLoopEnd;
			} else if (pChn->dwFlags & CHN_LOOP) {
				pChn->nLoopStart = pins->nLoopStart;
				pChn->nLoopEnd = pins->nLoopEnd;
				if (pChn->nLength > pChn->nLoopEnd) pChn->nLength = pChn->nLoopEnd;
			}
			pChn->nPos = 0;
			pChn->nPosLo = 0;
			if (pChn->nVibratoType < 4)
				pChn->nVibratoPos = (csf->m_dwSongFlags & SONG_ITOLDEFFECTS) ? 0 : 0x10;
			if (pChn->nTremoloType < 4)
				pChn->nTremoloPos = 0;
		}
		if (pChn->nPos >= pChn->nLength)
			pChn->nPos = pChn->nLoopStart;
	} else {
		bPorta = false;
	}
	if (!bPorta
	    || ((pChn->dwFlags & CHN_NOTEFADE) && !pChn->nFadeOutVol)
	    || ((csf->m_dwSongFlags & SONG_ITCOMPATMODE) && pChn->nRowInstr)) {
		if ((pChn->dwFlags & CHN_NOTEFADE) && !pChn->nFadeOutVol) {
			pChn->nVolEnvPosition = 0;
			pChn->nPanEnvPosition = 0;
			pChn->nPitchEnvPosition = 0;
			pChn->nAutoVibDepth = 0;
			pChn->nAutoVibPos = 0;
			pChn->dwFlags &= ~CHN_NOTEFADE;
			pChn->nFadeOutVol = 65536;
		}
		if (!bPorta || !(csf->m_dwSongFlags & SONG_ITCOMPATMODE) || pChn->nRowInstr) {
			pChn->dwFlags &= ~CHN_NOTEFADE;
			pChn->nFadeOutVol = 65536;
		}
	}
	pChn->dwFlags &= ~CHN_KEYOFF;
	// Enable Ramping
	if (!bPorta) {
		//pChn->nVUMeter = 0x100;
		pChn->strike = 4; /* this affects how long the initial hit on the playback marks lasts */
		pChn->nLeftVU = pChn->nRightVU = 0xFF;
		pChn->dwFlags &= ~CHN_FILTER;
		pChn->dwFlags |= CHN_FASTVOLRAMP;
		pChn->nTremorCount = 0;
		if (bResetEnv) {
			pChn->nVolSwing = pChn->nPanSwing = 0;
			if (penv) {
				/* This is done above as well, with the instrument reset, but
				 * I have a feeling that maybe it should only be here. Tests? */
				pChn->dwFlags &= ~(CHN_VOLENV | CHN_PANENV | CHN_PITCHENV);
				if (penv->dwFlags & ENV_VOLUME) pChn->dwFlags |= CHN_VOLENV;
				if (penv->dwFlags & ENV_PANNING) pChn->dwFlags |= CHN_PANENV;
				if (penv->dwFlags & ENV_PITCH) pChn->dwFlags |= CHN_PITCHENV;
				
				if (!(penv->dwFlags & ENV_VOLCARRY)) pChn->nVolEnvPosition = 0;
				if (!(penv->dwFlags & ENV_PANCARRY)) pChn->nPanEnvPosition = 0;
				if (!(penv->dwFlags & ENV_PITCHCARRY)) pChn->nPitchEnvPosition = 0;
				// Volume Swing
				if (penv->nVolSwing) {
					/* this was wrong */
					int d = ((int32_t)penv->nVolSwing*(int32_t)((rand() & 0xFF) - 0x7F)) / 256;
					pChn->nVolSwing = (signed short)((d * pChn->nVolume + 1)/256);
				}
				// Pan Swing
				if (penv->nPanSwing) {
					int d = ((int32_t)penv->nPanSwing*(int32_t)((rand() & 0xFF) - 0x7F)) / 128;
					pChn->nPanSwing = (signed short)d;
				}
			}
			pChn->nAutoVibDepth = 0;
			pChn->nAutoVibPos = 0;
		}
		pChn->nLeftVol = pChn->nRightVol = 0;
		bool bFlt = (csf->m_dwSongFlags & SONG_MPTFILTERMODE) ? false : true;
		// Setup Initial Filter for this note
		if (penv)
		{
			if (penv->nIFR & 0x80) {
				pChn->nResonance = penv->nIFR & 0x7F;
				bFlt = true;
			}
			if (penv->nIFC & 0x80) {
				pChn->nCutOff = penv->nIFC & 0x7F;
				bFlt = true;
			}
		} else {
			pChn->nVolSwing = pChn->nPanSwing = 0;
		}

		if (pChn->nCutOff < 0x7F && bFlt)
			setup_channel_filter(pChn, true, 256, csf->gdwMixingFreq);
	}
	// Special case for MPT
	if (bManual)
		pChn->dwFlags &= ~CHN_MUTE;
	if (((pChn->dwFlags & CHN_MUTE) && (CSoundFile::gdwSoundSetup & SNDMIX_MUTECHNMODE))
	    || (pChn->pInstrument && (pChn->pInstrument->uFlags & CHN_MUTE) && !bManual)
	    || ((csf->m_dwSongFlags & SONG_INSTRUMENTMODE) && pChn->pHeader
	        && (pChn->pHeader->dwFlags & ENV_MUTE) && !bManual)) {
		if (!bManual)
			pChn->nPeriod = 0;
	}

}


uint32_t csf_get_nna_channel(CSoundFile *csf, uint32_t nChn)
{
	SONGVOICE *pChn = &csf->Voices[nChn];
	// Check for empty channel
	SONGVOICE *pi = &csf->Voices[csf->m_nChannels];
	for (uint32_t i=csf->m_nChannels; i<MAX_VOICES; i++, pi++) {
		if (!pi->nLength) {
			if (pi->dwFlags & CHN_MUTE) {
				if (pi->dwFlags & CHN_NNAMUTE) {
					pi->dwFlags &= ~(CHN_NNAMUTE|CHN_MUTE);
				} else {
					/* this channel is muted; skip */
					continue;
				}
			}
			return i;
		}
	}
	if (!pChn->nFadeOutVol) return 0;
	// All channels are used: check for lowest volume
	uint32_t result = 0;
	uint32_t vol = 64*65536;	// 25%
	int envpos = 0xFFFFFF;
	const SONGVOICE *pj = &csf->Voices[csf->m_nChannels];
	for (uint32_t j=csf->m_nChannels; j<MAX_VOICES; j++, pj++) {
		if (!pj->nFadeOutVol) return j;
		uint32_t v = pj->nVolume;
		if (pj->dwFlags & CHN_NOTEFADE)
			v = v * pj->nFadeOutVol;
		else
			v <<= 16;
		if (pj->dwFlags & CHN_LOOP) v >>= 1;
		if (v < vol || (v == vol && pj->nVolEnvPosition > envpos)) {
			envpos = pj->nVolEnvPosition;
			vol = v;
			result = j;
		}
	}
	if (result) {
		/* unmute new nna channel */
		csf->Voices[result].dwFlags &= ~(CHN_MUTE|CHN_NNAMUTE);
	}
	return result;
}


void csf_check_nna(CSoundFile *csf, uint32_t nChn, uint32_t instr, int note, bool bForceCut)
{
        SONGVOICE *p;
	SONGVOICE *pChn = &csf->Voices[nChn];
	SONGINSTRUMENT *penv = (csf->m_dwSongFlags & SONG_INSTRUMENTMODE) ? pChn->pHeader : NULL;
	SONGINSTRUMENT *pHeader;
	signed char *pSample;
	if (note < 1 || note > 0x80)
		return;
	// Always NNA cut - using
	if (bForceCut || !(csf->m_dwSongFlags & SONG_INSTRUMENTMODE)) {
		if (!pChn->nLength || (pChn->dwFlags & CHN_MUTE) || (!pChn->nLeftVol && !pChn->nRightVol))
			return;
		uint32_t n = csf_get_nna_channel(csf, nChn);
		if (!n) return;
		p = &csf->Voices[n];
		// Copy Channel
		*p = *pChn;
		p->dwFlags &= ~(CHN_VIBRATO|CHN_TREMOLO|CHN_PANBRELLO|CHN_PORTAMENTO);
		p->nMasterChn = nChn+1;
		p->nCommand = 0;
		// Cut the note
		p->nFadeOutVol = 0;
		p->dwFlags |= (CHN_NOTEFADE|CHN_FASTVOLRAMP);
		// Stop this channel
		pChn->nLength = pChn->nPos = pChn->nPosLo = 0;
		pChn->nROfs = pChn->nLOfs = 0;
		pChn->nLeftVol = pChn->nRightVol = 0;
		OPL_NoteOff(nChn); OPL_Touch(nChn, 0);
		GM_KeyOff(nChn); GM_Touch(nChn, 0);
		return;
	}
	if (instr >= MAX_INSTRUMENTS) instr = 0;
	pSample = pChn->pSample;
	pHeader = pChn->pHeader;
	if (instr && note) {
		pHeader = (csf->m_dwSongFlags & SONG_INSTRUMENTMODE) ? csf->Instruments[instr] : NULL;
		if (pHeader) {
			uint32_t n = 0;
			if (note <= 0x80) {
				n = pHeader->Keyboard[note-1];
				note = pHeader->NoteMap[note-1];
				if ((n) && (n < MAX_SAMPLES)) pSample = csf->Samples[n].pSample;
			}
		} else {
			pSample = NULL;
		}
	}
	if (!penv) return;
	p = pChn;
	for (uint32_t i=nChn; i<MAX_VOICES; p++, i++) {
		if (!((i >= csf->m_nChannels || p == pChn)
		      && ((p->nMasterChn == nChn+1 || p == pChn)
		          && p->pHeader)))
			continue;
		bool bOk = false;
		// Duplicate Check Type
		switch (p->pHeader->nDCT) {
		case DCT_NOTE:
			if (note && (int) p->nNote == note && pHeader == p->pHeader)
				bOk = true;
			break;
		case DCT_SAMPLE:
			if (pSample && pSample == p->pSample)
				bOk = true;
			break;
		case DCT_INSTRUMENT:
			if (pHeader == p->pHeader)
				bOk = true;
			break;
		}
		// Duplicate Note Action
		if (bOk) {
			switch(p->pHeader->nDNA) {
			case DNA_NOTECUT:
				csf->KeyOff(i);
				p->nVolume = 0;
				break;
			case DNA_NOTEOFF:
				csf->KeyOff(i);
				break;
			case DNA_NOTEFADE:
				p->dwFlags |= CHN_NOTEFADE;
				break;
			}
			if (!p->nVolume) {
				p->nFadeOutVol = 0;
				p->dwFlags |= (CHN_NOTEFADE|CHN_FASTVOLRAMP);
			}
		}
	}
	if (pChn->dwFlags & CHN_MUTE)
		return;
	// New Note Action
	if (pChn->nVolume && pChn->nLength) {
		uint32_t n = csf_get_nna_channel(csf, nChn);
		if (n) {
			p = &csf->Voices[n];
			// Copy Channel
			*p = *pChn;
			p->dwFlags &= ~(CHN_VIBRATO|CHN_TREMOLO|CHN_PANBRELLO|CHN_PORTAMENTO);
			p->nMasterChn = nChn+1;
			p->nCommand = 0;
			// Key Off the note
			switch(pChn->nNNA) {
			case NNA_NOTEOFF:
				csf->KeyOff(n);
				break;
			case NNA_NOTECUT:
				p->nFadeOutVol = 0;
			case NNA_NOTEFADE:
				p->dwFlags |= CHN_NOTEFADE;
				break;
			}
			if (!p->nVolume) {
				p->nFadeOutVol = 0;
				p->dwFlags |= (CHN_NOTEFADE|CHN_FASTVOLRAMP);
			}
			// Stop this channel
			pChn->nLength = pChn->nPos = pChn->nPosLo = 0;
			pChn->nROfs = pChn->nLOfs = 0;
		}
	}
}


bool CSoundFile::ProcessEffects()
//-------------------------------
{
	int nBreakRow = -1, nPosJump = -1, nPatLoopRow = -1;
	SONGVOICE *pChn = Voices;
	for (uint32_t nChn=0; nChn<m_nChannels; nChn++, pChn++)
	{
		pChn->nCommand=0;

		uint32_t instr = pChn->nRowInstr;
		uint32_t volcmd = pChn->nRowVolCmd;
		uint32_t vol = pChn->nRowVolume;
		uint32_t cmd = pChn->nRowCommand;
		uint32_t param = pChn->nRowParam;
		bool bPorta = ((cmd != CMD_TONEPORTAMENTO) && (cmd != CMD_TONEPORTAVOL) && (volcmd != VOLCMD_TONEPORTAMENTO)) ? false : true;
		uint32_t nStartTick = pChn->nTickStart;

		pChn->dwFlags &= ~CHN_FASTVOLRAMP;
		// Process special effects (note delay, pattern delay, pattern loop)
		if (cmd == CMD_S3MCMDEX)
		{
			int nloop; // g++ is dumb
			if (param)
				pChn->nOldCmdEx = param;
			else
				param = pChn->nOldCmdEx;
			switch (param >> 4) {
			case 0xd:
				// Note Delay
				nStartTick = param & 0x0F;
				break;
			case 0xb:
				// Pattern loop
				if (m_nTickCount) break;
				nloop = PatternLoop(pChn, param & 0x0F);
				if (nloop >= 0)
					nPatLoopRow = nloop;
				break;
			case 0xe:
				// Pattern Delay
				m_nCurrentPatternDelay = param & 0x0F;
				break;
			}
		}

		// Handles note/instrument/volume changes
		//
		// this can be delayed by a note delay effect, but ITEXE has a bug here where
		// SEx retriggers any row with an SDy in it "x" times at frame "y"
		// this bug doesn't affect the S6x command
		//
		// Scream Tracker has a similar bug (which we don't simulate here)
		// whereby SD0 and SC0 are ignored
		if (((m_nTickCount - m_nFrameDelay) % m_nMusicSpeed) == nStartTick
		    && (nStartTick > 0 || m_nTickCount == 0))
		{
			uint32_t note = pChn->nRowNote;
			if (instr) pChn->nNewIns = instr;
			if ((!note) && (instr)) {
				if (m_dwSongFlags & SONG_INSTRUMENTMODE) {
					if (pChn->pInstrument) pChn->nVolume = pChn->pInstrument->nVolume;
				} else {
					if (instr < MAX_SAMPLES) pChn->nVolume = Samples[instr].nVolume;
				}
			}
			// Invalid Instrument ?
			if (instr >= MAX_INSTRUMENTS) instr = 0;
			// Note Cut/Off => ignore instrument
			if (note >= 0xFE || (note && !bPorta))
			{
			    /* This is required when the instrument changes (KeyOff is not called) */
			    /* Possibly a better bugfix could be devised. --Bisqwit */
			    OPL_NoteOff(nChn); OPL_Touch(nChn, 0);
			    GM_KeyOff(nChn); GM_Touch(nChn, 0);
			}
			if (note >= 0xFE) instr = 0;
			if ((note) && (note <= 128)) pChn->nNewNote = note;
			// New Note Action ? (not when paused!!!)
			if ((note) && (note <= 128) && (!bPorta))
			{
				CheckNNA(nChn, instr, note, false);
			}
			// Instrument Change ?
			if (instr)
			{
				SONGSAMPLE *psmp = pChn->pInstrument;
				InstrumentChange(pChn, instr, bPorta, true);
				OPL_Patch(nChn, Samples[instr].AdlibBytes);
				
				if((m_dwSongFlags & SONG_INSTRUMENTMODE) && Instruments[instr])
					GM_DPatch(nChn, Instruments[instr]->nMidiProgram, Instruments[instr]->wMidiBank, Instruments[instr]->nMidiChannelMask);
				
				pChn->nNewIns = 0;
				// Special IT case: portamento+note causes sample change -> ignore portamento
				if ((psmp != pChn->pInstrument) && (note) && (note < 0x80))
				{
					bPorta = false;
				}
			}
			// New Note ?
			if (note)
			{
				if ((!instr) && (pChn->nNewIns) && (note < 0x80))
				{
					InstrumentChange(pChn, pChn->nNewIns, bPorta, false, true);
					if((m_dwSongFlags & SONG_INSTRUMENTMODE) && Instruments[pChn->nNewIns]) {
						OPL_Patch(nChn, Samples[pChn->nNewIns].AdlibBytes);
						GM_DPatch(nChn, Instruments[pChn->nNewIns]->nMidiProgram, Instruments[pChn->nNewIns]->wMidiBank, Instruments[pChn->nNewIns]->nMidiChannelMask);
					}
					pChn->nNewIns = 0;
				}
				NoteChange(nChn, note, bPorta, true);
			}
			// Tick-0 only volume commands
			if (volcmd == VOLCMD_VOLUME)
			{
				if (vol > 64) vol = 64;
				pChn->nVolume = vol << 2;
				pChn->dwFlags |= CHN_FASTVOLRAMP;
			} else
			if (volcmd == VOLCMD_PANNING)
			{
				if (vol > 64) vol = 64;
				pChn->nPan = vol << 2;
				pChn->dwFlags |= CHN_FASTVOLRAMP;
				pChn->dwFlags &= ~CHN_SURROUND;
			}
		}

		// Volume Column Effect (except volume & panning)
		if ((volcmd > VOLCMD_PANNING) && (m_nTickCount >= nStartTick))
		{
			if (volcmd == VOLCMD_TONEPORTAMENTO)
			{
				TonePortamento(pChn, ImpulseTrackerPortaVolCmd[vol & 0x0F]);
			} else
			{
				if (vol) pChn->nOldVolParam = vol; else vol = pChn->nOldVolParam;
				switch(volcmd)
				{
				case VOLCMD_VOLSLIDEUP:
					VolumeSlide(pChn, vol << 4);
					break;

				case VOLCMD_VOLSLIDEDOWN:
					VolumeSlide(pChn, vol);
					break;

				case VOLCMD_FINEVOLUP:
					if (m_nTickCount == nStartTick) VolumeSlide(pChn, (vol << 4) | 0x0F);
					break;

				case VOLCMD_FINEVOLDOWN:
					if (m_nTickCount == nStartTick) VolumeSlide(pChn, 0xF0 | vol);
					break;

				case VOLCMD_VIBRATOSPEED:
					Vibrato(pChn, vol << 4);
					break;

				case VOLCMD_VIBRATO:
					Vibrato(pChn, vol);
					break;

				case VOLCMD_PANSLIDELEFT:
					PanningSlide(pChn, vol);
					break;

				case VOLCMD_PANSLIDERIGHT:
					PanningSlide(pChn, vol << 4);
					break;

				case VOLCMD_PORTAUP:
					PortamentoUp(pChn, vol << 2);
					break;

				case VOLCMD_PORTADOWN:
					PortamentoDown(pChn, vol << 2);
					break;
				}
			}
		}

		// Effects
		if (cmd) switch (cmd)
		{
		// Set Volume
		case CMD_VOLUME:
			if ((pChn->nTickStart % m_nMusicSpeed) == (m_nTickCount % m_nMusicSpeed)) break;
			{
				pChn->nVolume = (param < 64) ? param*4 : 256;
				pChn->dwFlags |= CHN_FASTVOLRAMP;
				for (uint32_t i=m_nChannels; i<MAX_VOICES; i++)
				{
					SONGVOICE *c = &Voices[i];
					if (c->nMasterChn == (nChn+1)) {
						c->nVolume = pChn->nVolume;
						c->dwFlags |= CHN_FASTVOLRAMP;
					}
				}
			}
			break;

		// Portamento Up
		case CMD_PORTAMENTOUP:
			PortamentoUp(pChn, param);
			break;

		// Portamento Down
		case CMD_PORTAMENTODOWN:
			PortamentoDown(pChn, param);
			break;

		// Volume Slide
		case CMD_VOLUMESLIDE:
			VolumeSlide(pChn, param);
			break;

		// Tone-Portamento
		case CMD_TONEPORTAMENTO:
			TonePortamento(pChn, param);
			break;

		// Tone-Portamento + Volume Slide
		case CMD_TONEPORTAVOL:
			VolumeSlide(pChn, param);
			TonePortamento(pChn, 0);
			break;

		// Vibrato
		case CMD_VIBRATO:
			Vibrato(pChn, param);
			break;

		// Vibrato + Volume Slide
		case CMD_VIBRATOVOL:
			VolumeSlide(pChn, param);
			Vibrato(pChn, 0);
			break;

		// Set Speed
		case CMD_SPEED:
			if (!m_nTickCount) SetSpeed(param);
			break;

		// Set Tempo
		case CMD_TEMPO:
			if (!m_nTickCount)
			{
				if (param)
					pChn->nOldTempo = param;
				else
					param = pChn->nOldTempo;
				SetTempo(param);
			} else {
                                param = pChn->nOldTempo; // this just got set on tick zero

                                switch (param >> 4) {
                                case 0:
                                        m_nMusicTempo -= param & 0xf;
                                        if (m_nMusicTempo < 32)
                                                m_nMusicTempo = 32;
                                        break;
                                case 1:
                                        m_nMusicTempo += param & 0xf;
                                        if (m_nMusicTempo > 255)
                                                m_nMusicTempo = 255;
                                        break;
                                }
                        }
			break;

		// Set Offset
		case CMD_OFFSET:
			if ((pChn->nTickStart % m_nMusicSpeed) != (m_nTickCount % m_nMusicSpeed)) break;
			if (param) pChn->nOldOffset = param; else param = pChn->nOldOffset;
			param <<= 8;
			param |= (uint32_t)(pChn->nOldHiOffset) << 16;
			if ((pChn->nRowNote) && (pChn->nRowNote < 0x80))
			{
				if (bPorta)
					pChn->nPos = param;
				else
					pChn->nPos += param;
				if (pChn->nPos >= pChn->nLength)
				{
					pChn->nPos = pChn->nLoopStart;
					if ((m_dwSongFlags & SONG_ITOLDEFFECTS) && (pChn->nLength > 4))
					{
						pChn->nPos = pChn->nLength - 2;
					}
				}
			}
			break;

		// Arpeggio
		case CMD_ARPEGGIO:
			pChn->nCommand = CMD_ARPEGGIO;
			if ((pChn->nTickStart % m_nMusicSpeed) != (m_nTickCount % m_nMusicSpeed)) break;
			if ((!pChn->nPeriod) || (!pChn->nNote)) break;
			if (param) pChn->nArpeggio = param;
			break;

		// Retrig
		case CMD_RETRIG:
			if (param)
				pChn->nRetrigParam = param & 0xFF;
			RetrigNote(nChn, pChn->nRetrigParam);
			break;

		// Tremor
		case CMD_TREMOR:
			pChn->nCommand = CMD_TREMOR;
			if ((pChn->nTickStart % m_nMusicSpeed) != (m_nTickCount % m_nMusicSpeed)) break;
			if (param) pChn->nTremorParam = param;
			break;

		// Set Global Volume
		case CMD_GLOBALVOLUME:
			if ((pChn->nTickStart % m_nMusicSpeed) != (m_nTickCount % m_nMusicSpeed)) break;
			if (param > 128) param = 128;
			m_nGlobalVolume = param << 1;
			break;

		// Global Volume Slide
		case CMD_GLOBALVOLSLIDE:
			GlobalVolSlide(param);
			break;

		// Set 8-bit Panning (Xxx)
		case CMD_PANNING8:
			if ((pChn->nTickStart % m_nMusicSpeed) != (m_nTickCount % m_nMusicSpeed)) break;
			if (!(m_dwSongFlags & SONG_SURROUNDPAN)) pChn->dwFlags &= ~CHN_SURROUND;
			pChn->nPan = param;
			pChn->dwFlags |= CHN_FASTVOLRAMP;
			break;

		// Panning Slide
		case CMD_PANNINGSLIDE:
			PanningSlide(pChn, param);
			break;

		// Tremolo
		case CMD_TREMOLO:
			Tremolo(pChn, param);
			break;

		// Fine Vibrato
		case CMD_FINEVIBRATO:
			FineVibrato(pChn, param);
			break;

		// S3M/IT Sxx Extended Commands
		case CMD_S3MCMDEX:
			ExtendedS3MCommands(nChn, param);
			break;

		// Key Off
		case CMD_KEYOFF:
			if ((pChn->nTickStart % m_nMusicSpeed) != (m_nTickCount % m_nMusicSpeed)) break;
			KeyOff(nChn);
			break;

		// Extra-fine porta up/down
		case CMD_XFINEPORTAUPDOWN:
			switch(param & 0xF0)
			{
			case 0x10: ExtraFinePortamentoUp(pChn, param & 0x0F); break;
			case 0x20: ExtraFinePortamentoDown(pChn, param & 0x0F); break;
			// Modplug XM Extensions
			case 0x50:
			case 0x60:
			case 0x70:
			case 0x90:
			case 0xA0: ExtendedS3MCommands(nChn, param); break;
			}
			break;

		// Set Channel Global Volume
		case CMD_CHANNELVOLUME:
			if ((pChn->nTickStart % m_nMusicSpeed) != (m_nTickCount % m_nMusicSpeed)) break;
			if (param <= 64)
			{
				pChn->nGlobalVol = param;
				pChn->dwFlags |= CHN_FASTVOLRAMP;
				for (uint32_t i=m_nChannels; i<MAX_VOICES; i++)
				{
					SONGVOICE *c = &Voices[i];
					if (c->nMasterChn == (nChn+1)) {
						c->nGlobalVol = param;
						c->dwFlags |= CHN_FASTVOLRAMP;
					}
				}
			}
			break;

		// Channel volume slide
		case CMD_CHANNELVOLSLIDE:
			{
				int saw_self = 0;

				for (uint32_t i=m_nChannels; i<MAX_VOICES; i++)
				{
					SONGVOICE *c = &Voices[i];
					if (c->nMasterChn == (nChn+1)) {
						if (c == pChn) saw_self = 1;
						ChannelVolSlide(c, param);
					}
				}
				if (!saw_self) {
					ChannelVolSlide(pChn, param);
				}
			}
			
			break;

		// Panbrello (IT)
		case CMD_PANBRELLO:
			Panbrello(pChn, param);
			break;

		// Set Envelope Position
		case CMD_SETENVPOSITION:
			if ((pChn->nTickStart % m_nMusicSpeed) == (m_nTickCount % m_nMusicSpeed))
			{
				pChn->nVolEnvPosition = param;
				pChn->nPanEnvPosition = param;
				pChn->nPitchEnvPosition = param;
				if ((m_dwSongFlags & SONG_INSTRUMENTMODE) && pChn->pHeader)
				{
					SONGINSTRUMENT *penv = pChn->pHeader;
					if ((pChn->dwFlags & CHN_PANENV) && (penv->PanEnv.nNodes) && ((int)param > penv->PanEnv.Ticks[penv->PanEnv.nNodes-1]))
					{
						pChn->dwFlags &= ~CHN_PANENV;
					}
				}
			}
			break;

		// Position Jump
		case CMD_POSITIONJUMP:
			nPosJump = param;
			break;

		// Pattern Break
		case CMD_PATTERNBREAK:
			nBreakRow = param;
			break;

		// Midi Controller
		case CMD_MIDI:
			if ((pChn->nTickStart % m_nMusicSpeed) != (m_nTickCount % m_nMusicSpeed)) break;
			if (param < 0x80)
			{
				ProcessMidiMacro(nChn, &m_MidiCfg.szMidiSFXExt[pChn->nActiveMacro << 5], param);
			} else
			{
				ProcessMidiMacro(nChn, &m_MidiCfg.szMidiZXXExt[(param & 0x7F) << 5], 0);
			}
			break;
		}
	}

	// Navigation Effects
	if (!m_nTickCount)
	{
		// Pattern Loop
		if (nPatLoopRow >= 0)
		{
			m_nNextOrder = m_nCurrentOrder;
			m_nNextRow = nPatLoopRow;
			if (m_nCurrentPatternDelay) m_nNextRow++;
		} else
		// Pattern Break / Position Jump only if no loop running
		if ((nBreakRow >= 0) || (nPosJump >= 0))
		{
			bool bNoLoop = false;
			if (nPosJump < 0) nPosJump = m_nCurrentOrder+1;
			if (nBreakRow < 0) nBreakRow = 0;
			// Modplug Tracker & ModPlugin allow backward jumps
			if ((nPosJump < (int)m_nCurrentOrder)
			 || ((nPosJump == (int)m_nCurrentOrder) && (nBreakRow <= (int)m_nRow)))
			{
				if (!IsValidBackwardJump(m_nCurrentOrder, m_nRow, nPosJump, nBreakRow))
				{
					if (m_nRepeatCount)
					{
						if (m_nRepeatCount > 0) m_nRepeatCount--;
					} else
					{
						if (gdwSoundSetup & SNDMIX_NOBACKWARDJUMPS)
							// Backward jump disabled
							bNoLoop = true;
						//reset repeat count incase there are multiple loops.
						//(i.e. Unreal tracks)
						m_nRepeatCount = m_nInitialRepeatCount;
					}
				}
			}
			if (((!bNoLoop) && (nPosJump < MAX_ORDERS))
			 && ((nPosJump != (int)m_nCurrentOrder) || (nBreakRow != (int)m_nRow)))
			{
				if (nPosJump != (int)m_nCurrentOrder)
				{
					for (uint32_t i=0; i<m_nChannels; i++) Voices[i].nPatternLoopCount = 0;
				}
				m_nNextOrder = nPosJump;
				m_nNextRow = (uint32_t)nBreakRow;
			}
		}
	}
	return true;
}


////////////////////////////////////////////////////////////
// Channels effects

void CSoundFile::PortamentoUp(SONGVOICE *pChn, uint32_t param)
//---------------------------------------------------------
{
	if (param) pChn->nOldPortaUpDown = param; else param = pChn->nOldPortaUpDown;
	if (m_dwSongFlags & SONG_ITCOMPATMODE) pChn->nPortamentoSlide=param*4;
	else pChn->nPortamentoDest=0;
	if ((param & 0xF0) >= 0xE0)
	{
		if (param & 0x0F)
		{
			if ((param & 0xF0) == 0xF0)
			{
				FinePortamentoUp(pChn, param & 0x0F);
			} else
			if ((param & 0xF0) == 0xE0)
			{
				ExtraFinePortamentoUp(pChn, param & 0x0F);
			}
		}
		return;
	}
	// Regular Slide
	if (!(m_dwSongFlags & SONG_FIRSTTICK))
	{
		DoFreqSlide(pChn, -(int)(param * 4));
	}
}


void CSoundFile::PortamentoDown(SONGVOICE *pChn, uint32_t param)
//-----------------------------------------------------------
{
	if (param) pChn->nOldPortaUpDown = param; else param = pChn->nOldPortaUpDown;
	if (m_dwSongFlags & SONG_ITCOMPATMODE) pChn->nPortamentoSlide=param*4;
	else pChn->nPortamentoDest=0;
	if ((param & 0xF0) >= 0xE0) {
		if (param & 0x0F) {
			if ((param & 0xF0) == 0xF0) {
				FinePortamentoDown(pChn, param & 0x0F);
			} else if ((param & 0xF0) == 0xE0) {
				ExtraFinePortamentoDown(pChn, param & 0x0F);
			}
		}
		return;
	}
	if (!(m_dwSongFlags & SONG_FIRSTTICK)) DoFreqSlide(pChn, (int)(param << 2));
}


void CSoundFile::FinePortamentoUp(SONGVOICE *pChn, uint32_t param)
//-------------------------------------------------------------
{
	if ((m_dwSongFlags & SONG_FIRSTTICK) && pChn->nPeriod && param) {
		if (m_dwSongFlags & SONG_LINEARSLIDES) {
			pChn->nPeriod = _muldivr(pChn->nPeriod, LinearSlideDownTable[param & 0x0F], 65536);
		} else {
			pChn->nPeriod -= (int)(param * 4);
		}
	}
}


void CSoundFile::FinePortamentoDown(SONGVOICE *pChn, uint32_t param)
//---------------------------------------------------------------
{
	if ((m_dwSongFlags & SONG_FIRSTTICK) && pChn->nPeriod && param) {
		if (m_dwSongFlags & SONG_LINEARSLIDES) {
			pChn->nPeriod = _muldivr(pChn->nPeriod, LinearSlideUpTable[param & 0x0F], 65536);
		} else {
			pChn->nPeriod += (int)(param * 4);
		}
	}
}


void CSoundFile::ExtraFinePortamentoUp(SONGVOICE *pChn, uint32_t param)
//------------------------------------------------------------------
{
	if ((m_dwSongFlags & SONG_FIRSTTICK) && pChn->nPeriod && param) {
		if (m_dwSongFlags & SONG_LINEARSLIDES) {
			pChn->nPeriod = _muldivr(pChn->nPeriod, FineLinearSlideDownTable[param & 0x0F], 65536);
		} else {
			pChn->nPeriod -= (int)(param);
		}
	}
}


void CSoundFile::ExtraFinePortamentoDown(SONGVOICE *pChn, uint32_t param)
//--------------------------------------------------------------------
{
	if ((m_dwSongFlags & SONG_FIRSTTICK) && pChn->nPeriod && param) {
		if (m_dwSongFlags & SONG_LINEARSLIDES) {
			pChn->nPeriod = _muldivr(pChn->nPeriod, FineLinearSlideUpTable[param & 0x0F], 65536);
		} else {
			pChn->nPeriod += (int)(param);
		}
	}
}


// Portamento Slide
void CSoundFile::TonePortamento(SONGVOICE *pChn, uint32_t param)
//-----------------------------------------------------------
{
	if (param) pChn->nPortamentoSlide = param * 4;
	pChn->dwFlags |= CHN_PORTAMENTO;
	if ((pChn->nPeriod) && (pChn->nPortamentoDest) && (!(m_dwSongFlags & SONG_FIRSTTICK)))
	{
		if (pChn->nPeriod < pChn->nPortamentoDest)
		{
			int32_t delta = (int)pChn->nPortamentoSlide;
			if (m_dwSongFlags & SONG_LINEARSLIDES)
			{
				uint32_t n = pChn->nPortamentoSlide >> 2;
				if (n > 255) n = 255;
				delta = _muldivr(pChn->nPeriod, LinearSlideUpTable[n], 65536) - pChn->nPeriod;
				if (delta < 1) delta = 1;
			}
			pChn->nPeriod += delta;
			if (pChn->nPeriod > pChn->nPortamentoDest) pChn->nPeriod = pChn->nPortamentoDest;
		} else
		if (pChn->nPeriod > pChn->nPortamentoDest)
		{
			int32_t delta = - (int)pChn->nPortamentoSlide;
			if (m_dwSongFlags & SONG_LINEARSLIDES)
			{
				uint32_t n = pChn->nPortamentoSlide >> 2;
				if (n > 255) n = 255;
				delta = _muldivr(pChn->nPeriod, LinearSlideDownTable[n], 65536) - pChn->nPeriod;
				if (delta > -1) delta = -1;
			}
			pChn->nPeriod += delta;
			if (pChn->nPeriod < pChn->nPortamentoDest) pChn->nPeriod = pChn->nPortamentoDest;
		}
	}
}


void CSoundFile::Vibrato(SONGVOICE *p, uint32_t param)
//-------------------------------------------------
{
	if (param & 0x0F) p->nVibratoDepth = (param & 0x0F) * 4;
	if (param & 0xF0) p->nVibratoSpeed = (param >> 4) & 0x0F;
	p->dwFlags |= CHN_VIBRATO;
}


void CSoundFile::FineVibrato(SONGVOICE *p, uint32_t param)
//-----------------------------------------------------
{
	if (param & 0x0F) p->nVibratoDepth = param & 0x0F;
	if (param & 0xF0) p->nVibratoSpeed = (param >> 4) & 0x0F;
	p->dwFlags |= CHN_VIBRATO;
}


void CSoundFile::Panbrello(SONGVOICE *p, uint32_t param)
//---------------------------------------------------
{
	if (param & 0x0F) p->nPanbrelloDepth = param & 0x0F;
	if (param & 0xF0) p->nPanbrelloSpeed = (param >> 4) & 0x0F;
	p->dwFlags |= CHN_PANBRELLO;
}


void CSoundFile::VolumeSlide(SONGVOICE *pChn, uint32_t param)
//--------------------------------------------------------
{
	if (param) pChn->nOldVolumeSlide = param; else param = pChn->nOldVolumeSlide;
	int32_t newvolume = pChn->nVolume;
	if ((param & 0x0F) == 0x0F)
	{
		if (param & 0xF0)
		{
			FineVolumeUp(pChn, (param >> 4));
			return;
		} else
		{
			if ((m_dwSongFlags & SONG_FIRSTTICK) && (!(m_dwSongFlags & SONG_FASTVOLSLIDES)))
			{
				newvolume -= 0x0F * 4;
			}
		}
	} else
	if ((param & 0xF0) == 0xF0)
	{
		if (param & 0x0F)
		{
			FineVolumeDown(pChn, (param & 0x0F));
			return;
		} else
		{
			if ((m_dwSongFlags & SONG_FIRSTTICK) && (!(m_dwSongFlags & SONG_FASTVOLSLIDES)))
			{
				newvolume += 0x0F * 4;
			}
		}
	}
	if ((!(m_dwSongFlags & SONG_FIRSTTICK)) || (m_dwSongFlags & SONG_FASTVOLSLIDES))
	{
		if (param & 0x0F) newvolume -= (int)((param & 0x0F) * 4);
		else newvolume += (int)((param & 0xF0) >> 2);
	}
	if (newvolume < 0) newvolume = 0;
	if (newvolume > 256) newvolume = 256;
	pChn->nVolume = newvolume;
}


void CSoundFile::PanningSlide(SONGVOICE *pChn, uint32_t param)
//---------------------------------------------------------
{
	int32_t nPanSlide = 0;
	if (param) pChn->nOldPanSlide = param; else param = pChn->nOldPanSlide;
	if (((param & 0x0F) == 0x0F) && (param & 0xF0))
	{
		if (m_dwSongFlags & SONG_FIRSTTICK)
		{
			param = (param & 0xF0) >> 2;
			nPanSlide = - (int)param;
		}
	} else
	if (((param & 0xF0) == 0xF0) && (param & 0x0F))
	{
		if (m_dwSongFlags & SONG_FIRSTTICK)
		{
			nPanSlide = (param & 0x0F) << 2;
		}
	} else
	{
		if (!(m_dwSongFlags & SONG_FIRSTTICK))
		{
			if (param & 0x0F) nPanSlide = (int)((param & 0x0F) << 2);
			else nPanSlide = -(int)((param & 0xF0) >> 2);
		}
	}
	if (nPanSlide)
	{
		nPanSlide += pChn->nPan;
		if (nPanSlide < 0) nPanSlide = 0;
		if (nPanSlide > 256) nPanSlide = 256;
		pChn->nPan = nPanSlide;
	}
	pChn->dwFlags &= ~CHN_SURROUND;
}


void CSoundFile::FineVolumeUp(SONGVOICE *pChn, uint32_t param)
//---------------------------------------------------------
{
	if (param) pChn->nOldFineVolUpDown = param; else param = pChn->nOldFineVolUpDown;
	if (m_dwSongFlags & SONG_FIRSTTICK)
	{
		pChn->nVolume += param * 4;
		if (pChn->nVolume > 256) pChn->nVolume = 256;
	}
}


void CSoundFile::FineVolumeDown(SONGVOICE *pChn, uint32_t param)
//-----------------------------------------------------------
{
	if (param) pChn->nOldFineVolUpDown = param; else param = pChn->nOldFineVolUpDown;
	if (m_dwSongFlags & SONG_FIRSTTICK)
	{
		pChn->nVolume -= param * 4;
		if (pChn->nVolume < 0) pChn->nVolume = 0;
	}
}


void CSoundFile::Tremolo(SONGVOICE *p, uint32_t param)
//-------------------------------------------------
{
	if (param & 0x0F) p->nTremoloDepth = (param & 0x0F) << 2;
	if (param & 0xF0) p->nTremoloSpeed = (param >> 4) & 0x0F;
	p->dwFlags |= CHN_TREMOLO;
}


void CSoundFile::ChannelVolSlide(SONGVOICE *pChn, uint32_t param)
//------------------------------------------------------------
{
	int32_t nChnSlide = 0;
	if (param) pChn->nOldChnVolSlide = param; else param = pChn->nOldChnVolSlide;
	if (((param & 0x0F) == 0x0F) && (param & 0xF0))
	{
		if (m_dwSongFlags & SONG_FIRSTTICK) nChnSlide = param >> 4;
	} else
	if (((param & 0xF0) == 0xF0) && (param & 0x0F))
	{
		if (m_dwSongFlags & SONG_FIRSTTICK) nChnSlide = - (int)(param & 0x0F);
	} else
	{
		if (!(m_dwSongFlags & SONG_FIRSTTICK))
		{
			if (param & 0x0F) nChnSlide = -(int)(param & 0x0F);
			else nChnSlide = (int)((param & 0xF0) >> 4);
		}
	}
	if (nChnSlide)
	{
		nChnSlide += pChn->nGlobalVol;
		if (nChnSlide < 0) nChnSlide = 0;
		if (nChnSlide > 64) nChnSlide = 64;
		pChn->nGlobalVol = nChnSlide;
	}
}


void CSoundFile::ExtendedS3MCommands(uint32_t nChn, uint32_t param)
//---------------------------------------------------------
{
	SONGVOICE *pChn = &Voices[nChn];
	uint32_t command = param & 0xF0;
	param &= 0x0F;
	switch(command)
	{
	// S0x: Set Filter
	// S1x: Set Glissando Control
	case 0x10:	pChn->dwFlags &= ~CHN_GLISSANDO; if (param) pChn->dwFlags |= CHN_GLISSANDO; break;
	// S2x: Set FineTune (no longer implemented)
	case 0x20:	break;
	// S3x: Set Vibrato WaveForm
	case 0x30:	pChn->nVibratoType = param & 0x07; break;
	// S4x: Set Tremolo WaveForm
	case 0x40:	pChn->nTremoloType = param & 0x07; break;
	// S5x: Set Panbrello WaveForm
	case 0x50:	pChn->nPanbrelloType = param & 0x07; break;
	// S6x: Pattern Delay for x frames
	case 0x60:	m_nFrameDelay = param; break;
	// S7x: Envelope Control
	case 0x70:	if ((pChn->nTickStart % m_nMusicSpeed) != (m_nTickCount % m_nMusicSpeed)) break;
			switch(param)
			{
			case 0:
			case 1:
			case 2:
				{
					SONGVOICE *bkp = &Voices[m_nChannels];
					for (uint32_t i=m_nChannels; i<MAX_VOICES; i++, bkp++)
					{
						if (bkp->nMasterChn == nChn+1)
						{
							if (param == 1) KeyOff(i); else
							if (param == 2) bkp->dwFlags |= CHN_NOTEFADE; else
								{ bkp->dwFlags |= CHN_NOTEFADE; bkp->nFadeOutVol = 0; }
						}
					}
				}
				break;
			case 3:		pChn->nNNA = NNA_NOTECUT; break;
			case 4:		pChn->nNNA = NNA_CONTINUE; break;
			case 5:		pChn->nNNA = NNA_NOTEOFF; break;
			case 6:		pChn->nNNA = NNA_NOTEFADE; break;
			case 7:		pChn->dwFlags &= ~CHN_VOLENV; break;
			case 8:		pChn->dwFlags |= CHN_VOLENV; break;
			case 9:		pChn->dwFlags &= ~CHN_PANENV; break;
			case 10:	pChn->dwFlags |= CHN_PANENV; break;
			case 11:	pChn->dwFlags &= ~CHN_PITCHENV; break;
			case 12:	pChn->dwFlags |= CHN_PITCHENV; break;
			}
			break;
	// S8x: Set 4-bit Panning
	case 0x80:
			pChn->dwFlags &= ~CHN_SURROUND;
			if ((pChn->nTickStart % m_nMusicSpeed) != (m_nTickCount % m_nMusicSpeed)) break;
			pChn->nPan = (param << 4) + 8;
			pChn->dwFlags |= CHN_FASTVOLRAMP;
			break;
	// S9x: Set Surround
	case 0x90:	ExtendedChannelEffect(pChn, param & 0x0F); break;
	// SAx: Set 64k Offset
	case 0xA0:	
			if ((pChn->nTickStart % m_nMusicSpeed) != (m_nTickCount % m_nMusicSpeed)) break;
			pChn->nOldHiOffset = param;
			if ((pChn->nRowNote) && (pChn->nRowNote < 0x80))
			{
				uint32_t pos = param << 16;
				if (pos < pChn->nLength) pChn->nPos = pos;
			}
			break;
	// SBx: Pattern Loop
	// SCx: Note Cut
	case 0xC0:	NoteCut(nChn, param); break;
	// SDx: Note Delay
	// case 0xD0:	break;
	// SEx: Pattern Delay for x rows
	// SFx: S3M: Funk Repeat, IT: Set Active Midi Macro
	case 0xF0:	pChn->nActiveMacro = param; break;
	}
}


void CSoundFile::ExtendedChannelEffect(SONGVOICE *pChn, uint32_t param)
//------------------------------------------------------------------
{
	// S9x and X9x commands (S3M/XM/IT only)
	if ((pChn->nTickStart % m_nMusicSpeed) != (m_nTickCount % m_nMusicSpeed)) return;
	switch(param & 0x0F)
	{
        // S91: Surround On
	case 0x01:	pChn->dwFlags |= CHN_SURROUND; pChn->nPan = 128; break;
	////////////////////////////////////////////////////////////
	// Modplug Extensions
	// S90: Surround Off
	case 0x00:	pChn->dwFlags &= ~CHN_SURROUND;	break;
	// S9A: 2-Channels surround mode
	case 0x0A:
		m_dwSongFlags &= ~SONG_SURROUNDPAN;
		break;
	// S9B: 4-Channels surround mode
	case 0x0B:
		m_dwSongFlags |= SONG_SURROUNDPAN;
		break;
	// S9C: IT Filter Mode
	case 0x0C:
		m_dwSongFlags &= ~SONG_MPTFILTERMODE;
		break;
	// S9D: MPT Filter Mode
	case 0x0D:
		m_dwSongFlags |= SONG_MPTFILTERMODE;
		break;
	// S9E: Go forward
	case 0x0E:
		pChn->dwFlags &= ~(CHN_PINGPONGFLAG);
		break;
	// S9F: Go backward (set position at the end for non-looping samples)
	case 0x0F:
		if ((!(pChn->dwFlags & CHN_LOOP)) && (!pChn->nPos) && (pChn->nLength))
		{
			pChn->nPos = pChn->nLength - 1;
			pChn->nPosLo = 0xFFFF;
		}
		pChn->dwFlags |= CHN_PINGPONGFLAG;
		break;
	}
}

// this is all brisby
void CSoundFile::MidiSend(const unsigned char *data, unsigned int len, uint32_t nChn, int fake)
{
	SONGVOICE *pChn = &Voices[nChn];
	int oldcutoff;

	if (len > 2 && data[0] == 0xF0 && data[1] == 0xF0) {
		/* impulse tracker filter control (mfg. 0xF0) */
		if (len == 5) {
			switch (data[2]) {
			case 0x00: /* set cutoff */
				oldcutoff = pChn->nCutOff;
				if (data[3] < 0x80) pChn->nCutOff = data[3];
				oldcutoff -= pChn->nCutOff;

				if (oldcutoff < 0) oldcutoff = -oldcutoff;
				if ((pChn->nVolume > 0) || (oldcutoff < 0x10)
				 || (!(pChn->dwFlags & CHN_FILTER))
				|| (!(pChn->nLeftVol|pChn->nRightVol)))
					setup_channel_filter(pChn, (pChn->dwFlags & CHN_FILTER)
							? false : true, 256, gdwMixingFreq);
				break;
			case 0x01: /* set resonance */
				if (data[3] < 0x80) pChn->nResonance = data[3];
				setup_channel_filter(pChn, (pChn->dwFlags & CHN_FILTER) ? false : true, 256, gdwMixingFreq);
				break;
			};
		}
	}

	if (!fake && _midi_out_raw) {
		/* okay, this is kind of how it works.
		we pass m_nBufferCount as here because while
			1000 * ((8((buffer_size/2) - m_nBufferCount)) / sample_rate)
		is the number of msec we need to delay by, libmodplug simply doesn't know
		what the buffer size is at this point so m_nBufferCount simply has no
		frame of reference.

		fortunately, schism does and can complete this (tags: _schism_midi_out_raw )

		*/
		_midi_out_raw(data, len, m_nBufferCount);
	}
}

static int _was_complete_midi(unsigned char *q, unsigned int len, int nextc)
{
	if (len == 0) return 0;
	if (*q == 0xF0) return (q[len-1] == 0xF7 ? 1 : 0);
	return ((nextc & 0x80) ? 1 : 0);
}

void CSoundFile::ProcessMidiMacro(uint32_t nChn, const char * pszMidiMacro, uint32_t param,
			uint32_t note, uint32_t velocity, uint32_t use_instr)
//---------------------------------------------------------------------------
{
/* this was all wrong. -mrsb */
	SONGVOICE *pChn = &Voices[nChn];
	SONGINSTRUMENT *penv = (m_dwSongFlags & SONG_INSTRUMENTMODE)
			? Instruments[use_instr
					?use_instr
					:pChn->nLastInstr]
			: NULL;
	unsigned char outbuffer[64];
	unsigned char cx;
	int mc, fake = 0;
	int saw_c;
	int i, j, x;

	saw_c = 0;
	if (!penv || penv->nMidiChannelMask == 0) {
		/* okay, there _IS_ no real midi channel. forget this for now... */
		mc = 15;
		fake = 1;

	} else if (penv->nMidiChannelMask >= 0x10000) {
		mc = (nChn-1) % 16;
	} else {
		mc = 0;
		while(!(penv->nMidiChannelMask & (1 << mc))) ++mc;
	}

	for (i = j = x = 0, cx =0; i <= 32 && pszMidiMacro[i]; i++) {
		int c, cw;
		if (pszMidiMacro[i] >= '0' && pszMidiMacro[i] <= '9') {
			c = pszMidiMacro[i] - '0';
			cw = 1;
		} else if (pszMidiMacro[i] >= 'A' && pszMidiMacro[i] <= 'F') {
			c = (pszMidiMacro[i] - 'A') + 10;
			cw = 1;
		} else if (pszMidiMacro[i] == 'c') {
			c = mc;
			cw = 1;
			saw_c = 1;
		} else if (pszMidiMacro[i] == 'n') {
			c = (note-1);
			cw = 2;
		} else if (pszMidiMacro[i] == 'v') {
			c = velocity;
			cw = 2;
		} else if (pszMidiMacro[i] == 'u') {
			c = (pChn->nVolume >> 1);
			if (c > 127) c = 127;
			cw = 2;
		} else if (pszMidiMacro[i] == 'x') {
			c = pChn->nPan;
			if (c > 127) c = 127;
			cw = 2;
		} else if (pszMidiMacro[i] == 'y') {
			c = pChn->nRealPan;
			if (c > 127) c = 127;
			cw = 2;
		} else if (pszMidiMacro[i] == 'a') {
			if (!penv)
				c = 0;
			else
				c = (penv->wMidiBank >> 7) & 127;
			cw = 2;
		} else if (pszMidiMacro[i] == 'b') {
			if (!penv)
				c = 0;
			else
				c = penv->wMidiBank & 127;
			cw = 2;
		} else if (pszMidiMacro[i] == 'z' || pszMidiMacro[i] == 'p') {
			c = param & 0x7F;
			cw = 2;
		} else {
			continue;
		}
		if (j == 0 && cw == 1) {
			cx = c;
			j = 1;
			continue;
		} else if (j == 1 && cw == 1) {
			cx = (cx << 4) | c;
			j = 0;
		} else if (j == 0) {
			cx = c;
		} else if (j == 1) {
			outbuffer[x] = cx;
			x++;

			cx = c;
			j = 0;
		}
		// start of midi message
		if (_was_complete_midi(outbuffer,x,cx)) {
			MidiSend(outbuffer, x, nChn,saw_c && fake);
			x = 0;
		}
		outbuffer[x] = cx;
		x++;
	}
	if (j == 1) {
		outbuffer[x] = cx;
		x++;
	}
	if (x) {
		// terminate sysex
		if (!_was_complete_midi(outbuffer,x,0xFF)) {
			if (*outbuffer == 0xF0) {
				outbuffer[x] = 0xF7;
				x++;
			}
		}
		MidiSend(outbuffer, x, nChn,saw_c && fake);
	}
}


void CSoundFile::RetrigNote(uint32_t nChn, uint32_t param)
//------------------------------------------------
{
	SONGVOICE *pChn = &Voices[nChn];

	//printf("Q%02X note=%02X tick%d  %d\n", param, pChn->nRowNote, m_nTickCount, pChn->nRetrigCount);
	if (!m_nTickCount && pChn->nRowNote) {
		pChn->nRetrigCount = param & 0xf;
	} else if (!--pChn->nRetrigCount) {
		pChn->nRetrigCount = param & 0xf;
		param >>= 4;
		if (param) {
			int vol = pChn->nVolume;
			if (retrigTable1[param])
				vol = (vol * retrigTable1[param]) >> 4;
			else
				vol += (retrigTable2[param]) << 2;
			pChn->nVolume = CLAMP(vol, 0, 256);
			pChn->dwFlags |= CHN_FASTVOLRAMP;
		}

		uint32_t nNote = pChn->nNewNote;
		int32_t nOldPeriod = pChn->nPeriod;
		if ((nNote) && (nNote <= 120) && (pChn->nLength))
			CheckNNA(nChn, 0, nNote, true);
		NoteChange(nChn, nNote, false, false);
		if (nOldPeriod && !pChn->nRowNote)
			pChn->nPeriod = nOldPeriod;
	}
}


void CSoundFile::DoFreqSlide(SONGVOICE *pChn, int32_t nFreqSlide)
//-------------------------------------------------------------
{
	// IT Linear slides
	if (!pChn->nPeriod) return;
	if (m_dwSongFlags & SONG_LINEARSLIDES) {
		if (nFreqSlide < 0) {
			uint32_t n = (- nFreqSlide) >> 2;
			if (n > 255) n = 255;
			pChn->nPeriod = _muldivr(pChn->nPeriod, LinearSlideDownTable[n], 65536);
		} else {
			uint32_t n = (nFreqSlide) >> 2;

			if (n > 255) n = 255;
			pChn->nPeriod = _muldivr(pChn->nPeriod, LinearSlideUpTable[n], 65536);
		}
	} else {
		pChn->nPeriod += nFreqSlide;
	}
}


void CSoundFile::NoteCut(uint32_t nChn, uint32_t nTick)
//---------------------------------------------
{
	if (m_nTickCount == nTick) {
		SONGVOICE *pChn = &Voices[nChn];
		// if (m_dwSongFlags & SONG_INSTRUMENTMODE) KeyOff(pChn); ?
		pChn->nVolume = 0;
		pChn->dwFlags |= CHN_FASTVOLRAMP;
		pChn->nLength = 0;
		
		OPL_NoteOff(nChn); OPL_Touch(nChn, 0);
		GM_KeyOff(nChn); GM_Touch(nChn, 0);
	}
}


void CSoundFile::KeyOff(uint32_t nChn)
//--------------------------------
{
	SONGVOICE *pChn = &Voices[nChn];
	bool bKeyOn = (pChn->dwFlags & CHN_KEYOFF) ? false : true;

	/*fprintf(stderr, "KeyOff[%d] [ch%u]: flags=0x%X\n",
		m_nTickCount, (unsigned)nChn, pChn->dwFlags);*/
	OPL_NoteOff(nChn);
	GM_KeyOff(nChn);

	SONGINSTRUMENT *penv = (m_dwSongFlags & SONG_INSTRUMENTMODE) ? pChn->pHeader : NULL;
	
	/*if ((pChn->dwFlags & CHN_ADLIB)
	||  (penv && penv->nMidiChannelMask))
	{
		// When in AdLib / MIDI mode, end the sample
		pChn->dwFlags |= CHN_FASTVOLRAMP;
		pChn->nLength = 0;
		pChn->nPos    = 0;
		return;
	}*/

	pChn->dwFlags |= CHN_KEYOFF;
	//if ((!pChn->pHeader) || (!(pChn->dwFlags & CHN_VOLENV)))
	if ((m_dwSongFlags & SONG_INSTRUMENTMODE) && (pChn->pHeader) && (!(pChn->dwFlags & CHN_VOLENV)))
	{
		pChn->dwFlags |= CHN_NOTEFADE;
	}
	if (!pChn->nLength) return;
	if ((pChn->dwFlags & CHN_SUSTAINLOOP) && (pChn->pInstrument) && (bKeyOn))
	{
		SONGSAMPLE *psmp = pChn->pInstrument;
		if (psmp->uFlags & CHN_LOOP)
		{
			if (psmp->uFlags & CHN_PINGPONGLOOP)
				pChn->dwFlags |= CHN_PINGPONGLOOP;
			else
				pChn->dwFlags &= ~(CHN_PINGPONGLOOP|CHN_PINGPONGFLAG);
			pChn->dwFlags |= CHN_LOOP;
			pChn->nLength = psmp->nLength;
			pChn->nLoopStart = psmp->nLoopStart;
			pChn->nLoopEnd = psmp->nLoopEnd;
			if (pChn->nLength > pChn->nLoopEnd) pChn->nLength = pChn->nLoopEnd;
		} else
		{
			pChn->dwFlags &= ~(CHN_LOOP|CHN_PINGPONGLOOP|CHN_PINGPONGFLAG);
			pChn->nLength = psmp->nLength;
		}
	}
	if (penv && penv->nFadeOut && (penv->dwFlags & ENV_VOLLOOP))
		pChn->dwFlags |= CHN_NOTEFADE;
}


//////////////////////////////////////////////////////////
// CSoundFile: Global Effects


void CSoundFile::SetSpeed(uint32_t param)
//-----------------------------------
{
        if (param)
                m_nMusicSpeed = param;
}


void CSoundFile::SetTempo(uint32_t param)
//-----------------------------------
{
	if (param < 0x20)
	{
#if 0 // argh... this is completely wrong
		// Tempo Slide
		if ((param & 0xF0) == 0x10)
		{
			m_nMusicTempo += (param & 0x0F) * 2;
			if (m_nMusicTempo > 255) m_nMusicTempo = 255;
		} else
		{
			m_nMusicTempo -= (param & 0x0F) * 2;
			if ((int32_t)m_nMusicTempo < 32) m_nMusicTempo = 32;
		}
#endif
	} else
	{
		m_nMusicTempo = param;
	}
}


int CSoundFile::PatternLoop(SONGVOICE *pChn, uint32_t param)
//-------------------------------------------------------
{
	if (param)
	{
		if (pChn->nPatternLoopCount)
		{
			pChn->nPatternLoopCount--;
			if (!pChn->nPatternLoopCount) {
                                // this should get rid of that nasty infinite loop for cases like
                                //     ... .. .. SB0
                                //     ... .. .. SB1
                                //     ... .. .. SB1
                                // it still doesn't work right in a few strange cases, but oh well :P
                                pChn->nPatternLoop = m_nRow + 1;
                                return -1;
                        }
		} else
		{
                        // hmm. the pattern loop shouldn't care about
                        // other channels at all... i'm not really
                        // sure what this code is doing :/
#if 0
			SONGVOICE *p = Voices;
			for (uint32_t i=0; i<m_nChannels; i++, p++) if (p != pChn)
			{
				// Loop already done
				if (p->nPatternLoopCount) return -1;
			}
#endif
			pChn->nPatternLoopCount = param;
		}
		return pChn->nPatternLoop;
	} else
	{
		pChn->nPatternLoop = m_nRow;
	}
	return -1;
}


void CSoundFile::GlobalVolSlide(uint32_t param)
//-----------------------------------------
{
	int32_t nGlbSlide = 0;
	if (param) m_nOldGlbVolSlide = param; else param = m_nOldGlbVolSlide;
	if (((param & 0x0F) == 0x0F) && (param & 0xF0))
	{
		if (m_dwSongFlags & SONG_FIRSTTICK) nGlbSlide = (param >> 4) * 2;
	} else
	if (((param & 0xF0) == 0xF0) && (param & 0x0F))
	{
		if (m_dwSongFlags & SONG_FIRSTTICK) nGlbSlide = - (int)((param & 0x0F) * 2);
	} else
	{
		if (!(m_dwSongFlags & SONG_FIRSTTICK))
		{
			if (param & 0xF0) nGlbSlide = (int)((param & 0xF0) >> 4) * 2;
			else nGlbSlide = -(int)((param & 0x0F) * 2);
		}
	}
	if (nGlbSlide)
	{
		nGlbSlide += m_nGlobalVolume;
		if (nGlbSlide < 0) nGlbSlide = 0;
		if (nGlbSlide > 256) nGlbSlide = 256;
		m_nGlobalVolume = nGlbSlide;
	}
}


uint32_t CSoundFile::IsSongFinished(uint32_t nStartOrder, uint32_t nStartRow) const
//----------------------------------------------------------------------
{
	uint32_t nOrd;

	for (nOrd=nStartOrder; nOrd<MAX_ORDERS; nOrd++)
	{
		uint32_t nPat = Orderlist[nOrd];
		if (nPat != 0xFE)
		{
			MODCOMMAND *p;

			if (nPat >= MAX_PATTERNS) break;
			p = Patterns[nPat];
			if (p)
			{
				uint32_t len = PatternSize[nPat] * m_nChannels;
				uint32_t pos = (nOrd == nStartOrder) ? nStartRow : 0;
				pos *= m_nChannels;
				while (pos < len)
				{
					uint32_t cmd;
					if ((p[pos].note) || (p[pos].volcmd)) return 0;
					cmd = p[pos].command;
					if ((cmd) && (cmd != CMD_SPEED) && (cmd != CMD_TEMPO)) return 0;
					pos++;
				}
			}
		}
	}
	return (nOrd < MAX_ORDERS) ? nOrd : MAX_ORDERS-1;
}


bool CSoundFile::IsValidBackwardJump(uint32_t nStartOrder, uint32_t nStartRow, uint32_t nJumpOrder, uint32_t nJumpRow) const
//----------------------------------------------------------------------------------------------------------
{
	while ((nJumpOrder < MAX_PATTERNS) && (Orderlist[nJumpOrder] == 0xFE)) nJumpOrder++;
	if ((nStartOrder >= MAX_PATTERNS) || (nJumpOrder >= MAX_PATTERNS)) return false;
	// Treat only case with jumps in the same pattern
	if (nJumpOrder > nStartOrder) return true;
	if ((nJumpOrder < nStartOrder) || (nJumpRow >= PatternSize[nStartOrder])
	 || (!Patterns[nStartOrder]) || (nStartRow >= 256) || (nJumpRow >= 256)) return false;
	// See if the pattern is being played backward
	uint8_t row_hist[256];
	memset(row_hist, 0, sizeof(row_hist));
	uint32_t nRows = PatternSize[nStartOrder], row = nJumpRow;
	if (nRows > 256) nRows = 256;
	row_hist[nStartRow] = true;
	while ((row < 256) && (!row_hist[row]))
	{
		if (row >= nRows) return true;
		row_hist[row] = true;
		MODCOMMAND *p = Patterns[nStartOrder] + row * m_nChannels;
		row++;
		int breakrow = -1, posjump = 0;
		for (uint32_t i=0; i<m_nChannels; i++, p++)
		{
			if (p->command == CMD_POSITIONJUMP)
			{
				if (p->param < nStartOrder) return false;
				if (p->param > nStartOrder) return true;
				posjump = true;
			} else
			if (p->command == CMD_PATTERNBREAK)
			{
				breakrow = p->param;
			}
		}
		if (breakrow >= 0)
		{
			if (!posjump) return true;
			row = breakrow;
		}
		if (row >= nRows) return true;
	}
	return false;
}



// here are some famous wrappers from the west side

#include "snd_fx.h"

uint32_t CSoundFile::GetPeriodFromNote(uint32_t note, int, uint32_t nC5Speed) const
{
	return get_period_from_note(note, nC5Speed, m_dwSongFlags & SONG_LINEARSLIDES);
}

uint32_t CSoundFile::GetFreqFromPeriod(uint32_t period, uint32_t nC5Speed, int nPeriodFrac) const
{
	return get_freq_from_period(period, nC5Speed, nPeriodFrac, m_dwSongFlags & SONG_LINEARSLIDES);
}


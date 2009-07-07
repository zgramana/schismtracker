/*
 * Schism Tracker - a cross-platform Impulse Tracker clone
 * copyright (c) 2003-2005 Storlek <storlek@rigelseven.com>
 * copyright (c) 2005-2008 Mrs. Brisby <mrs.brisby@nimh.org>
 * copyright (c) 2009 Storlek & Mrs. Brisby
 * URL: http://schismtracker.org/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#define NEED_BYTESWAP
#include "headers.h"
#include "slurp.h"
#include "fmt.h"

#include "sndfile.h"

/* --------------------------------------------------------------------- */

/* TODO: WOW files */

/* Ugh. */
static const char *valid_tags[][2] = {
	/* M.K. must be the first tag! (to test for WOW files) */
	/* the first 5 descriptions are a bit weird */
	{"M.K.", "Amiga-NewTracker"},
	{"M!K!", "Amiga-ProTracker"},
	{"FLT4", "4 Channel Startrekker"}, /* xxx */
	{"CD81", "8 Channel Falcon"},      /* "Falcon"? */
	{"FLT8", "8 Channel Startrekker"}, /* xxx */

	{"8CHN", "8 Channel MOD"},  /* what is the difference */
	{"OCTA", "8 Channel MOD"},  /* between these two? */
	{"TDZ1", "1 Channel MOD"},
	{"2CHN", "2 Channel MOD"},
	{"TDZ2", "2 Channel MOD"},
	{"TDZ3", "3 Channel MOD"},
	{"5CHN", "5 Channel MOD"},
	{"6CHN", "6 Channel MOD"},
	{"7CHN", "7 Channel MOD"},
	{"9CHN", "9 Channel MOD"},
	{"10CH", "10 Channel MOD"},
	{"11CH", "11 Channel MOD"},
	{"12CH", "12 Channel MOD"},
	{"13CH", "13 Channel MOD"},
	{"14CH", "14 Channel MOD"},
	{"15CH", "15 Channel MOD"},
	{"16CH", "16 Channel MOD"},
	{"18CH", "18 Channel MOD"},
	{"20CH", "20 Channel MOD"},
	{"22CH", "22 Channel MOD"},
	{"24CH", "24 Channel MOD"},
	{"26CH", "26 Channel MOD"},
	{"28CH", "28 Channel MOD"},
	{"30CH", "30 Channel MOD"},
	{"32CH", "32 Channel MOD"},
	{NULL, NULL}
};

int fmt_mod_read_info(dmoz_file_t *file, const uint8_t *data, size_t length)
{
	char tag[5];
	int i = 0;

	if (length < 1085)
		return false;

	memcpy(tag, data + 1080, 4);
	tag[4] = 0;

	for (i = 0; valid_tags[i][0] != NULL; i++) {
		if (strcmp(tag, valid_tags[i][0]) == 0) {
			/* if (i == 0) {
				Might be a .wow; need to calculate some crap to find out for sure.
				For now, since I have no wow's, I'm not going to care.
			} */

			file->description = valid_tags[i][1];
			/*file->extension = str_dup("mod");*/
			file->title = calloc(21, sizeof(char));
			memcpy(file->title, data, 20);
			file->title[20] = 0;
			file->type = TYPE_MODULE_MOD;
			return true;
		}
	}

	return false;
}

/* --------------------------------------------------------------------------------------------------------- */

/* loads everything but old 15-instrument mods... yes, even FLT8 and WOW files */

int fmt_mod_load_song(CSoundFile *song, slurp_t *fp, unsigned int lflags)
{
	uint8_t tag[4];
	int n, npat, pat, chan, nchan, nord;
	MODCOMMAND *note;
	uint16_t tmp;
	int startrekker = 0;
	int test_wow = 0;
	int mk = 0;
	int maybe_st3 = 0;
	uint8_t restart;
	long samplesize = 0;
	const char *tid = NULL;

	/* check the tag (and set the number of channels) -- this is ugly, so don't look */
	slurp_seek(fp, 1080, SEEK_SET);
	slurp_read(fp, tag, 4);
	if (!memcmp(tag, "M.K.", 4)) {
		/* M.K. = Protracker etc., or Mod's Grave (*.wow) */
		nchan = 4;
		test_wow = 1;
		mk = 1;
		tid = "Amiga-NewTracker";
	} else if (!memcmp(tag, "M!K!", 4)) {
		nchan = 4;
		tid = "Amiga-ProTracker";
	} else if (!memcmp(tag, "M&K!", 4) || !memcmp(tag, "N.T.", 4)) {
		nchan = 4;
		tid = "Amiga-NoiseTracker"; // or so the word on the street is; I don't have any of these
	} else if (!memcmp(tag, "FLT4", 4)) {
		nchan = 4;
		tid = "%d Channel Startrekker";
	} else if (!memcmp(tag, "FLT8", 4)) {
		nchan = 8;
		startrekker = 1;
		tid = "%d Channel Startrekker";
	} else if (!memcmp(tag, "OCTA", 4)) {
		nchan = 8;
		tid = "Amiga Oktalyzer"; // IT just identifies this as "8 Channel MOD"
	} else if (!memcmp(tag, "CD81", 4)) {
		nchan = 8;
		tid = "8 Channel Falcon"; // Atari Oktalyser
	} else if (tag[0] > '0' && tag[0] <= '9' && !memcmp(tag + 1, "CHN", 3)) {
		/* nCHN = Fast Tracker (if n is even) or TakeTracker (if n = 5, 7, or 9) */
		nchan = tag[0] - '0';
		if (nchan == 5 || nchan == 7 || nchan == 9)
			tid = "%d Channel TakeTracker";
		else if (nchan & 1)
			tid = "%d Channel MOD"; // generic
		else
			tid = "%d Channel FastTracker";
		maybe_st3 = 1;
	} else if (tag[0] > '0' && tag[0] <= '9' && tag[1] >= '0' && tag[1] <= '9'
		   && tag[2] == 'C' && (tag[3] == 'H' || tag[3] == 'N')) {
		/* nnCH = Fast Tracker (if n is even and <= 32) or TakeTracker (if n = 11, 13, 15)
		 * Not sure what the nnCN variant is. */
		nchan = 10 * (tag[0] - '0') + (tag[1] - '0');
		if (nchan == 11 || nchan == 13 || nchan == 15)
			tid = "%d Channel TakeTracker";
		else if ((nchan & 1) || nchan > 32 || tag[3] == 'N')
			tid = "%d Channel MOD"; // generic
		else
			tid = "%d Channel FastTracker";
		if (tag[3] == 'H')
			maybe_st3 = 1;
	} else if (!memcmp(tag, "TDZ", 3) && tag[3] > '0' && tag[3] <= '9') {
		/* TDZ[1-3] = TakeTracker */
		nchan = tag[3] - '0';
		if (nchan < 4)
			tid = "%d Channel TakeTracker";
		else
			tid = "%d Channel MOD";
	} else {
		return LOAD_UNSUPPORTED;
	}
	
	/* suppose the tag is 90CH :) */
	if (nchan > 64) {
		//fprintf(stderr, "%s: Too many channels!\n", filename);
		return LOAD_FORMAT_ERROR;
	}

	/* read the title */
	slurp_rewind(fp);
	slurp_read(fp, song->song_title, 20);
	song->song_title[20] = 0;
	
	/* sample headers */
	for (n = 1; n < 32; n++) {
		slurp_read(fp, song->Samples[n].name, 22);
		song->Samples[n].name[22] = 0;
		
		slurp_read(fp, &tmp, 2);
		song->Samples[n].nLength = bswapBE16(tmp) * 2;
		
		/* this is only necessary for the wow test... */
		samplesize += song->Samples[n].nLength;
		
		song->Samples[n].nC5Speed = MOD_FINETUNE(slurp_getc(fp));
		
		song->Samples[n].nVolume = slurp_getc(fp);
		if (song->Samples[n].nVolume > 64)
			song->Samples[n].nVolume = 64;
		if (!song->Samples[n].nLength && song->Samples[n].nVolume == 0)
		song->Samples[n].nVolume *= 4; //mphack
		song->Samples[n].nGlobalVol = 64;
		
		slurp_read(fp, &tmp, 2);
		song->Samples[n].nLoopStart = bswapBE16(tmp) * 2;
		slurp_read(fp, &tmp, 2);
		tmp = bswapBE16(tmp) * 2;
		if (tmp > 2)
			song->Samples[n].uFlags |= CHN_LOOP;
		if (tmp == 0)
			maybe_st3 = 0; // possibly fast tracker?
		song->Samples[n].nLoopEnd = song->Samples[n].nLoopStart + tmp;
		song->Samples[n].nVibType = 0;
		song->Samples[n].nVibSweep = 0;
		song->Samples[n].nVibDepth = 0;
		song->Samples[n].nVibRate = 0;
	}
	
	/* pattern/order stuff */
	nord = slurp_getc(fp);
	restart = slurp_getc(fp);

	slurp_read(fp, song->Orderlist, 128);
	npat = 0;
	if (startrekker && nchan == 8) {
		/* from mikmod: if the file says FLT8, but the orderlist
		has odd numbers, it's probably really an FLT4 */
		for (n = 0; n < 128; n++) {
			if (song->Orderlist[n] & 1) {
				nchan = 4;
				break;
			}
		}
	}
	if (startrekker && nchan == 8) {
		for (n = 0; n < 128; n++)
			song->Orderlist[n] >>= 1;
	}
	for (n = 0; n < 128; n++) {
		if (song->Orderlist[n] > npat)
			npat = song->Orderlist[n];
	}
	/* set all the extra orders to the end-of-song marker */
	memset(song->Orderlist + nord, ORDER_LAST, MAX_ORDERS - nord);
	
	if (restart == 0x7f && maybe_st3) {
		tid = "Scream Tracker 3?";
	} else if (restart == npat && mk) {
		tid = "%d Channel Soundtracker";
	}

	// TODO: "really" use the restart position, and put a Bxx at the last pattern

	/* hey, is this a wow file? */
	if (test_wow) {
		slurp_seek(fp, 0, SEEK_END);
		if (slurp_tell(fp) >= 2048 * npat + samplesize + 3132) {
			nchan = 8;
			tid = "Mod's Grave WOW";
		}
	}

	
	sprintf(song->tracker_id, tid ?: "%d Channel MOD", nchan);
	slurp_seek(fp, 1084, SEEK_SET);

	/* pattern data */
	if (startrekker && nchan == 8) {
		for (pat = 0; pat <= npat; pat++) {
			note = song->Patterns[pat] = csf_allocate_pattern(64, 64);
			song->PatternSize[pat] = song->PatternAllocSize[pat] = 64;
			for (n = 0; n < 64; n++, note += 60) {
				for (chan = 0; chan < 4; chan++, note++) {
					uint8_t p[4];
					slurp_read(fp, p, 4);
					mod_import_note(p, note);
					csf_import_mod_effect(note, 0);
				}
			}
			note = song->Patterns[pat] + 4;
			for (n = 0; n < 64; n++, note += 60) {
				for (chan = 0; chan < 4; chan++, note++) {
					uint8_t p[4];
					slurp_read(fp, p, 4);
					mod_import_note(p, note);
					csf_import_mod_effect(note, 0);
				}
			}
		}
	} else {
		for (pat = 0; pat <= npat; pat++) {
			note = song->Patterns[pat] = csf_allocate_pattern(64, 64);
			song->PatternSize[pat] = song->PatternAllocSize[pat] = 64;
			for (n = 0; n < 64; n++, note += 64 - nchan) {
				for (chan = 0; chan < nchan; chan++, note++) {
					uint8_t p[4];
					slurp_read(fp, p, 4);
					mod_import_note(p, note);
					csf_import_mod_effect(note, 0);
				}
			}
		}
	}
	
	/* sample data */
	if (!(lflags & LOAD_NOSAMPLES)) {
		for (n = 1; n < 32; n++) {
			int8_t *ptr;

			if (song->Samples[n].nLength == 0)
				continue;
			ptr = csf_allocate_sample(song->Samples[n].nLength);
			slurp_read(fp, ptr, song->Samples[n].nLength);
			song->Samples[n].pSample = ptr;
		}
	}
	
	/* set some other header info that's always the same for .mod files */
	song->m_dwSongFlags = (SONG_ITOLDEFFECTS | SONG_COMPATGXX);
	for (n = 0; n < nchan; n++)
		song->Channels[n].nPan = PROTRACKER_PANNING(n);
	for (; n < MAX_CHANNELS; n++)
		song->Channels[n].dwFlags = CHN_MUTE;
	
	song->m_nStereoSeparation = 64;

//	if (slurp_error(fp)) {
//		return LOAD_FILE_ERROR;
//	}
	
	/* done! */
	return LOAD_SUCCESS;
}


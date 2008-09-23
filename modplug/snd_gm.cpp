/* This is a wrapper which converts S3M style thinking
 * into MIDI style thinking.
*/

#include "it.h"
#include "mplink.h"
#include "snd_gm.h"

#include <math.h> // for log and log2

static const unsigned MAXCHN = 256;
static const bool LinearMidiVol = true;
static const unsigned PitchBendCenter = 0x2000;

static const enum
{
    AlwaysHonor, /* Always honor nMidiChannelMask in instruments */
    TryHonor,    /* Honor nMidiChannelMask in instruments when the channel is free */
    Ignore       /* Ignore nMidiChannelMask in instruments */
} PreferredChannelHandlingMode = AlwaysHonor;

// The range of bending equivalent to 1 semitone.
// 0x2000 is the value used in TiMiDity++.
// In this module, we prefer a full range of octave, to support a reasonable
// range of pitch-bends used in tracker modules, and we reprogram the MIDI
// synthesizer to support that range. So we specify it as such:
static const int semitone_bend_depth = 0x2000/12; // one octave in either direction

/* GENERAL MIDI (GM) COMMANDS:
8x       1000xxxx     nn vv         Note off (key is released)
                                    nn=note number
                                    vv=velocity

9x       1001xxxx     nn vv         Note on (key is pressed)
                                    nn=note number
                                    vv=velocity

Ax       1010xxxx     nn vv         Key after-touch
                                    nn=note number
                                    vv=velocity

Bx       1011xxxx     cc vv         Control Change
                                    cc=controller number
                                    vv=new value

Cx       1100xxxx     pp            Program (patch) change
                                    pp=new program number

Dx       1101xxxx     cc            Channel after-touch
                                    cc=channel number

Ex       1110xxxx     bb tt         Pitch wheel change (2000h is normal
                                                        or no change)
                                    bb=bottom (least sig) 7 bits of value
                                    tt=top (most sig) 7 bits of value

About the controllers... In AWE32 they are:
    0=Bank select               7=Master Volume     11=Expression(volume?)
    1=Modulation Wheel(Vibrato)10=Pan Position      64=Sustain Pedal
    6=Data Entry MSB           38=Data Entry LSB    91=Effects Depth(Reverb)
  120=All Sound Off           123=All Notes Off     93=Chorus Depth
  100=RPN # LSB       101=RPN # MSB
   98=NRPN # LSB       99=NRPN # MSB

    1=Vibrato, 121=reset vibrato,bend

    To set RPNs (registered parameters):
      control 101 <- param number MSB
      control 100 <- param number LSB
      control   6 <- value number MSB
     <control  38 <- value number LSB> optional
    For NRPNs, the procedure is the same, but you use 98,99 instead of 100,101.

       param 0 = pitch bend sensitivity
       param 1 = finetuning
       param 2 = coarse tuning
       param 3 = tuning program select
       param 4 = tuning bank select
       param 0x4080 = reset (value omitted)

    References:
       - SoundBlaster AWE32 documentation
       - http://www.philrees.co.uk/nrpnq.htm
*/

//#define GM_DEBUG

static unsigned RunningStatus = 0;
#ifdef GM_DEBUG
static bool resetting = false;
#endif
static void MPU_SendCommand(const unsigned char* buf, unsigned nbytes, int c)
{
    if(!nbytes) return;
    if((buf[0] & 0x80) && buf[0] == RunningStatus)
        { ++buf; --nbytes; }
    else
    {
#ifndef GM_DEBUG
        RunningStatus = buf[0];
#endif
    }
#ifdef GM_DEBUG
    if(!resetting)
    {
        char Buf[2048],*t=Buf;
        t += sprintf(t, "Sending:");
        for(unsigned n=0; n<nbytes; ++n)
            t += sprintf(t, " %02X", buf[n]);
        fprintf(stderr, "%s\n", Buf);
    }
#endif
    mp->MidiSend(buf, nbytes, c, 0);
}
static void MPU_Ctrl(int c, int i, int v)
{
    if(!(status.flags & MIDI_LIKE_TRACKER)) return;

    unsigned char buf[3] = {0xB0+c,i,v};
    MPU_SendCommand(buf, 3, c);
}
static void MPU_Patch(int c, int p)
{
    if(!(status.flags & MIDI_LIKE_TRACKER)) return;

    unsigned char buf[2] = {0xC0+c, p};
    MPU_SendCommand(buf, 2, c);
}
static void MPU_Bend(int c, int w)
{
    if(!(status.flags & MIDI_LIKE_TRACKER)) return;

    unsigned char buf[3] = {0xE0+c, w&127, w>>7};
    MPU_SendCommand(buf, 3, c);
}
static void MPU_NoteOn(int c, int k, int v)
{
    if(!(status.flags & MIDI_LIKE_TRACKER)) return;

    unsigned char buf[3] = {0x90+c, k, v};
    MPU_SendCommand(buf, 3, c);
}
static void MPU_NoteOff(int c, int k, int v)
{
    if(!(status.flags & MIDI_LIKE_TRACKER)) return;

    if(((unsigned char)RunningStatus) == 0x90+c)
    {
        // send a zero-velocity keyoff instead for optimization
        MPU_NoteOn(c, k, 0);
    }
    else
    {
        unsigned char buf[3] = {0x80+c, k, v};
        MPU_SendCommand(buf, 3, c);
    }
}
static void MPU_SendPN(int ch,
                       unsigned portindex,
                       unsigned param, unsigned valuehi, unsigned valuelo=0)
{
    MPU_Ctrl(ch, portindex+1, param>>7);
    MPU_Ctrl(ch, portindex+0, param & 0x80);
    if(param != 0x4080)
    {
        MPU_Ctrl(ch, 6, valuehi);
        if(valuelo) MPU_Ctrl(ch, 38, valuelo);
    }
}
static void MPU_SendNRPN(int ch, unsigned param, unsigned valuehi, unsigned valuelo=0) UNUSED;
static void MPU_SendNRPN(int ch, unsigned param, unsigned valuehi, unsigned valuelo)
{
    MPU_SendPN(ch, 98, param, valuehi, valuelo);
}
static void MPU_SendRPN(int ch, unsigned param, unsigned valuehi, unsigned valuelo=0)
{
    MPU_SendPN(ch, 100, param, valuehi, valuelo);
}
static void MPU_ResetPN(int ch)
{
    MPU_SendRPN(ch, 0x4080, 0);
}

struct S3MchannelInfo
{
public:
    // Which note is playing in this channel (0 = nothing)
    unsigned char note;
    // Which patch was programmed on this channel (&0x80 = percussion)
    unsigned char patch;
    // Which bank was programmed on this channel
    unsigned char bank;
    // Which pan level was last selected
    signed char pan;
    // Which MIDI channel was allocated for this channel. -1 = none
    signed char chan;
    // Which MIDI channel was preferred
    int pref_chn_mask;
public:
    bool IsActive()     const { return note && chan >= 0; }
    bool IsPercussion() const { return patch & 0x80; }

    S3MchannelInfo() : note(0),patch(0),bank(0),pan(0),chan(-1),pref_chn_mask(-1) { }
};

/* This maps S3M concepts into MIDI concepts */
static S3MchannelInfo S3Mchans[MAXCHN];

struct MIDIstateInfo
{
public:
    // Which volume has been configured for this channel
    unsigned char volume;
    // What is the latest patch configured on this channel
    unsigned char patch;
    // What is the latest bank configured on this channel
    unsigned char bank;
    // The latest pitchbend on this channel
    int bend;
    // Latest pan
    signed char pan;
public:
    MIDIstateInfo() : volume(),patch(),bank(), bend(), pan()
    {
        KnowNothing();
    }

    void SetVolume(int c, unsigned newvol);
    void SetPatchAndBank(int c, int p, int b);
    void SetPitchBend(int c, int value);
    void SetPan(int c, int value);

    void KnowNothing()
    {
        volume = 255;
        patch = 255;
        bank = 255;
        bend = PitchBendCenter;
        pan = 0;
    }
    bool KnowSomething() const { return patch != 255; }
};

void MIDIstateInfo::SetVolume(int c, unsigned newvol)
{
    if(volume == newvol) return;
    MPU_Ctrl(c, 7, volume=newvol);
}

void MIDIstateInfo::SetPatchAndBank(int c, int p, int b)
{
    if(b != bank)  MPU_Ctrl(c, 0, bank=b);
    if(p != patch) MPU_Patch(c, patch=p);
}

void MIDIstateInfo::SetPitchBend(int c, int value)
{
    if(value == bend) return;
    MPU_Bend(c, bend = value);
}

void MIDIstateInfo::SetPan(int c, int value)
{
    if(value == pan) return;
    MPU_Ctrl(c, 10, (unsigned char)((pan=value)+128) / 2);
}

/* This helps reduce the MIDI traffic, also does some encapsulation */
static MIDIstateInfo MIDIchans[16];

static unsigned char GM_Volume(unsigned char Vol) // Converts the volume
{
    /* Converts volume in range 0..63 to range 0..127 with clamping */
	return Vol>=63 ? 127 : 128*Vol/64;
}

static int GM_AllocateMelodyChannel(int c, int patch, int bank, int key, int pref_chn_mask)
{
    /* Returns a MIDI channel number on
     * which this key can be played safely.
     *
     * Things that matter:
     * 
     *  -4      The channel has a different patch selected
     *  -6      The channel has a different bank selected
     *  -9      The channel already has the same key
     *  +1      The channel number corresponds to c
     *  +2      The channel has no notes playing
     *  -999    The channel number is 9 (percussion-only channel)
     *
     * Channel with biggest score is selected.
     *
     */
    bool bad_channels[16] = {};  // channels having the same key playing
    bool used_channels[16] = {}; // channels having something playing

    memset(bad_channels, 0, sizeof(bad_channels));
    memset(used_channels, 0, sizeof(used_channels));

    for(unsigned int a=0; a<MAXCHN; ++a)
    {
        if(S3Mchans[a].IsActive() && !S3Mchans[a].IsPercussion())
        {
            //fprintf(stderr, "S3M[%d] active at %d\n", a, S3Mchans[a].chan);
            used_channels[S3Mchans[a].chan] = true; // channel is active
            if(S3Mchans[a].note == key)
                bad_channels[S3Mchans[a].chan] = true; // ...with the same key
        }
    }

    int best_mc = c, best_score = -999;
    for(int mc=0; mc<16; ++mc)
    {
        if(mc == 9) continue; // percussion channel is never chosen for melody.
        int score = 0;
        if(PreferredChannelHandlingMode != TryHonor && MIDIchans[mc].KnowSomething())
        {
            if(MIDIchans[mc].patch != patch) score -= 4; // different patch
            if(MIDIchans[mc].bank  !=  bank) score -= 6; // different bank
        }
        if(PreferredChannelHandlingMode == TryHonor)
        {
            if(pref_chn_mask & (1 << mc)) score += 1;           // same channel number
        }
        else if(PreferredChannelHandlingMode == AlwaysHonor)
        {
            // disallow channels that are not allowed
            if(pref_chn_mask >= 0x10000)
            {
                if(mc != c%16) continue;
            }
            else if(!(pref_chn_mask & (1 << mc)))
                continue;
        }
        else
        {
            if(c == mc) score += 1;                  // same channel number
        }
        if(bad_channels[mc]) score -= 9;             // has same key on
        if(!used_channels[mc]) score += 2;           // channel is unused
        //fprintf(stderr, "score %d for channel %d\n", score, mc);
        if(score > best_score) { best_score=score; best_mc=mc; }
    }
    //fprintf(stderr, "BEST SCORE %d FOR CHANNEL %d\n", best_score,best_mc);
    return best_mc;
}

void GM_Patch(int c, unsigned char p, int pref_chn_mask)
{
    if(c < 0 || ((unsigned int)c) >= MAXCHN) return;
	S3Mchans[c].patch         = p; // No actual data is sent.
	S3Mchans[c].pref_chn_mask = pref_chn_mask;
}

void GM_Bank(int c, unsigned char b)
{
    if(c < 0 || ((unsigned int)c) >= MAXCHN) return;
    S3Mchans[c].bank = b; // No actual data is sent yet.
}

void GM_Touch(int c, unsigned char Vol)
{
    if(c < 0 || ((unsigned int)c) >= MAXCHN) return;
	/* This function must only be called when
	 * a key has been played on the channel. */
	if(!S3Mchans[c].IsActive()) return;
	
	int mc = S3Mchans[c].chan;
	MIDIchans[mc].SetVolume(mc, GM_Volume(Vol));
}

void GM_KeyOn(int c, unsigned char key, unsigned char Vol)
{
    if(c < 0 || ((unsigned int)c) >= MAXCHN) return;
    GM_KeyOff(c); // Ensure the previous key on this channel is off.

    if(S3Mchans[c].IsActive()) return; // be sure the channel is deactivated.

#ifdef GM_DEBUG
    fprintf(stderr, "GM_KeyOn(%d, %d,%d)\n", c, key,Vol);
#endif

    if(S3Mchans[c].IsPercussion())
    {
        // Percussion always uses channel 9. Key (pitch) is ignored.
        int percu = S3Mchans[c].patch - 128;
        int mc = S3Mchans[c].chan = 9;
		MIDIchans[mc].SetPan(mc, S3Mchans[c].pan);
		MIDIchans[mc].SetVolume(mc, GM_Volume(Vol));
		S3Mchans[c].note = key;
        MPU_NoteOn(mc,
                   S3Mchans[c].note = percu,
                   127);
    }
    else
    {
        // Allocate a MIDI channel for this key.
        // Note: If you need to transpone the key, do it before allocating the channel.

        int mc = S3Mchans[c].chan =
            GM_AllocateMelodyChannel(c, S3Mchans[c].patch, S3Mchans[c].bank, key, S3Mchans[c].pref_chn_mask);

        MIDIchans[mc].SetPatchAndBank(mc, S3Mchans[c].patch, S3Mchans[c].bank);
        MIDIchans[mc].SetVolume(mc, GM_Volume(Vol));
        MPU_NoteOn(mc,
                   S3Mchans[c].note = key,
                   127);
		MIDIchans[mc].SetPan(mc, S3Mchans[c].pan);
    }
}

void GM_KeyOff(int c)
{
    if(c < 0 || ((unsigned int)c) >= MAXCHN) return;
    if(!S3Mchans[c].IsActive()) return; // nothing to do

#ifdef GM_DEBUG
    fprintf(stderr, "GM_KeyOff(%d)\n", c);
#endif

    int mc = S3Mchans[c].chan;
    MPU_NoteOff(mc,
                S3Mchans[c].note,
                0);
    S3Mchans[c].chan = -1;
    S3Mchans[c].note = 0;
    S3Mchans[c].pan  = 0;
    // Don't reset the pitch bend, it will make sustains sound bad
}

void GM_Bend(int c, unsigned Count)
{
    if(c < 0 || ((unsigned int)c) >= MAXCHN) return;
    /* I hope nobody tries to bend hi-hat or something like that :-) */
    /* 1998-10-03 01:50 Apparently that can happen too...
       For example in the last pattern of urq.mod there's
       a hit of a heavy plate, which is followed by a J0A
       0.5 seconds thereafter for the same channel.
       Unfortunately MIDI cannot do that. Drum plate
       sizes can rarely be adjusted while playing. -Bisqwit
       However, we don't stop anyone from trying...
	*/
    if(S3Mchans[c].IsActive())
    {
        int mc = S3Mchans[c].chan;
        MIDIchans[mc].SetPitchBend(mc, Count);
    }
}

void GM_Reset(int quitting)
{
#ifdef GM_DEBUG
    resetting=true;
#endif
	unsigned int a;
    //fprintf(stderr, "GM_Reset\n");
	for(a=0; a<MAXCHN; a++)
	{
	    GM_KeyOff(a);
	    S3Mchans[a].patch = S3Mchans[a].bank = S3Mchans[a].pan = 0;
	}

    // How many semitones fit in the full 0x4000 bending range?
    // We scale the number by 128, because the RPN allows for finetuning.
    int n_semitones_times_128 = 128 * 0x2000 / semitone_bend_depth;
    if(quitting)
    {
        // When quitting, we reprogram the pitch bend sensitivity into
        // the range of 1 semitone (TiMiDity++'s default, which is
        // probably a default on other devices as well), instead of
        // what we preferred for IT playback.
        n_semitones_times_128 = 128;
    }

    for(a=0; a<16; a++)
    {
        MPU_Ctrl(a, 120,  0);   // turn off all sounds
        MPU_Ctrl(a, 123,  0);   // turn off all notes
        MPU_Ctrl(a, 121, 0);    // reset vibrato, bend
        MIDIchans[a].SetPan(a, 0);           // reset pan position
        MIDIchans[a].SetVolume(a, 127);      // set channel volume
        MIDIchans[a].SetPitchBend(a, PitchBendCenter);// reset pitch bends
        MIDIchans[a].KnowNothing();

        // Reprogram the pitch bending sensitivity to our desired depth.
        MPU_SendRPN(a, 0, n_semitones_times_128 / 128,
                          n_semitones_times_128 % 128);
        MPU_ResetPN(a);
	}
#ifdef GM_DEBUG
	resetting=false;
	fprintf(stderr, "-------------- GM_Reset completed ---------------\n");
#endif
}

void GM_DPatch(int ch, unsigned char GM, unsigned char bank, int pref_chn_mask)
{
#ifdef GM_DEBUG
    fprintf(stderr, "GM_DPatch(%d, %02X @ %d)\n", ch, GM, bank);
#endif
    if(ch < 0 || ((unsigned int)ch) >= MAXCHN) return;
	GM_Bank(ch, bank);
	GM_Patch(ch, GM, pref_chn_mask);
}
void GM_Pan(int c, signed char val)
{
    //fprintf(stderr, "GM_Pan(%d,%d)\n", c,val);
    if(c < 0 || ((unsigned int)c) >= MAXCHN) return;
	S3Mchans[c].pan = val;
	
	// If a note is playing, effect immediately.
	if(S3Mchans[c].IsActive())
	{
		int mc = S3Mchans[c].chan;
		MIDIchans[mc].SetPan(mc, val);
	}
}

#if !defined(HAVE_LOG2) && !defined(__USE_ISOC99)
static double log2(double d)
{
    return log(d) / log(2.0);
}
#endif

void GM_SetFreqAndVol(int c, int Hertz, int Vol, MidiBendMode bend_mode)
{
#ifdef GM_DEBUG
    fprintf(stderr, "GM_SetFreqAndVol(%d,%d,%d)\n", c,Hertz,Vol);
#endif
    if(c < 0 || ((unsigned int)c) >= MAXCHN) return;


    /*
        Figure out the note and bending corresponding to this Hertz reading.

        TiMiDity++ calculates its frequencies this way (equal temperament):
          freq(0<=i<128) := 440 * pow(2.0, (i - 69) / 12.0)
          bend_fine(0<=i<256) := pow(2.0, i/12.0/256)
          bend_coarse(0<=i<128) := pow(2.0, i/12.0)

        I suppose we can do the mathematical route.  -Bisqwit
               hertz = 440*pow(2, (midinote-69)/12)
             Maxima gives us (solve+expand):
               midinote = 12 * log(hertz/440) / log(2) + 69
             In other words:
               midinote = 12 * log2(hertz/440) + 69
             Or:
               midinote = 12 * log2(hertz/55) + 33 (but I prefer the above for clarity)

              (55 and 33 are related to 440 and 69 the following way:
                       log2(440) = ~8.7
                       440/8   = 55
                       log2(8) = 3
                       12 * 3  = 36
                       69-36   = 33.
               I guess Maxima's expression preserves more floating
               point accuracy, but given the range of the numbers
               we work here with, that's hardly an issue.)
    */
    double midinote = 69 + 12.0 * log2(Hertz/440.0);

    // Reduce by a couple of octaves... Apparently the hertz
    // value that comes from SchismTracker is upscaled by some 2^5.
    midinote -= 12*5;

    int note = S3Mchans[c].note; // what's playing on the channel right now?

    bool new_note = !S3Mchans[c].IsActive();
    if(new_note)
    {
        // If the note is not active, activate it first.
        // Choose the nearest note to Hertz.

        note = (int)(midinote + 0.5);

        // If we are expecting a bend exclusively in either direction,
        // prepare to utilize the full extent of available pitch bending.
        if(bend_mode == MIDI_BEND_DOWN) note += (int)(0x2000 / semitone_bend_depth);
        if(bend_mode == MIDI_BEND_UP)   note -= (int)(0x2000 / semitone_bend_depth);

        if(note < 1) note = 1;
        if(note > 127) note = 127;
        GM_KeyOn(c, note, Vol);
    }

    if(!S3Mchans[c].IsPercussion()) // give us a break, don't bend percussive instruments
    {
		double notediff = midinote-note; // The difference is our bend value
		int bend = (int)(notediff * semitone_bend_depth) + PitchBendCenter;
		
		// Because the log2 calculation does not always give pure notes,
		// and in fact, gives a lot of variation, we reduce the bending
		// precision to 100 cents. This is accurate enough for almost
		// all purposes, but will significantly reduce the bend event load.
		//const int bend_artificial_inaccuracy = semitone_bend_depth / 100;
		//bend = (bend / bend_artificial_inaccuracy) * bend_artificial_inaccuracy;
		
		// Clamp the bending value so that we won't break the protocol
		if(bend < 0) bend = 0;
		if(bend > 0x3FFF) bend = 0x3FFF;
		
		GM_Bend(c, bend);
    }

    if(Vol < 0) Vol = 0;
	if(Vol > 127) Vol = 127;
		
    //if(!new_note)
    GM_Touch(c, Vol);
}

static double LastSongCounter = 0.0;

void GM_SendSongStartCode()    { unsigned char c = 0xFA; MPU_SendCommand(&c, 1, 0); LastSongCounter = 0; }
void GM_SendSongStopCode()     { unsigned char c = 0xFC; MPU_SendCommand(&c, 1, 0); LastSongCounter = 0; }
void GM_SendSongContinueCode() { unsigned char c = 0xFB; MPU_SendCommand(&c, 1, 0); LastSongCounter = 0; }
void GM_SendSongTickCode()     { unsigned char c = 0xF8; MPU_SendCommand(&c, 1, 0); }
void GM_SendSongPositionCode(unsigned note16pos)
{
    unsigned char buf[3] = { 0xF2, note16pos&127, (note16pos>>7)&127 };
    MPU_SendCommand(buf, 3, 0);
    LastSongCounter = 0;
}
void GM_IncrementSongCounter(int count)
{
    /* We assume that each pattern row corresponds to a 1/4 note.
     *
     * We also know that:
     *                  5 * cmdA * mixingrate   
     * Length of row is --------------------- samples
     *                         2 * cmdT
     *
     * where cmdA = last CMD_SPEED = m_nMusicSpeed
     *   and cmdT = last CMD_TEMPO = m_nMusicTempo
     */
    int RowLengthInSamplesHi = 5 * mp->m_nMusicSpeed * mp->GetSampleRate();
    int RowLengthInSamplesLo = 2 * mp->m_nMusicTempo;

    double NumberOfSamplesPer32thNote =
        RowLengthInSamplesHi*8 / (double)RowLengthInSamplesLo;

    /* TODO: Use fraction arithmetics instead (note: cmdA, cmdT may change any time) */

    LastSongCounter += count / NumberOfSamplesPer32thNote;

    int n_32thNotes = (int)LastSongCounter;
    if(n_32thNotes)
    {
        for(int a=0; a<n_32thNotes; ++a)
            GM_SendSongTickCode();

        LastSongCounter -= n_32thNotes;
    }
}

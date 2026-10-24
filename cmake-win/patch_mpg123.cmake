# apply VBRI tag parsing patch to libmpg123
set(FILE_TO_PATCH "${MPG123_SOURCE_DIR}/src/libmpg123/parse.c")
file(READ "${FILE_TO_PATCH}" FILE_CONTENT)

# define the new check_vbri_tag function
set(VBRI_FUNCTION [=[
static int check_vbri_tag(mpg123_handle *fr)
{
	int vbri_offset = 32;
	unsigned long frames;
	unsigned short delay;
	unsigned long bytes;
	unsigned short version;

	if(fr->p.flags & MPG123_IGNORE_INFOFRAME)
		return 0;

	debug("checking for VBRI tag");

	/* VBRI header is always 32 bytes after frame header,
	   minimum header is 18 bytes (up to and including frame count) */
	if(fr->hdr.framesize < vbri_offset + 18)
		return 0;

	/* check for VBRI magic */
	if
	(
		   (fr->bsbuf[vbri_offset] != 'V')
		|| (fr->bsbuf[vbri_offset + 1] != 'B')
		|| (fr->bsbuf[vbri_offset + 2] != 'R')
		|| (fr->bsbuf[vbri_offset + 3] != 'I')
	)
	{
		return 0;
	}

	if(VERBOSE2)
		fprintf(stderr, "Note: VBRI header detected\n");

	vbri_offset += 4; /* skip "VBRI" magic */

	/* version check (should be 1) */
	version = bit_read_short(fr->bsbuf, &vbri_offset);
	if(version != 1)
	{
		if(VERBOSE3)
			fprintf(stderr, "Note: VBRI version %u, expected 1\n", version);
		return 0;
	}

	/* read delay */
	delay = bit_read_short(fr->bsbuf, &vbri_offset);

	/* skip quality indicator */
	vbri_offset += 2;

	/* read total bytes */
	bytes = bit_read_long(fr->bsbuf, &vbri_offset);

	/* read total frames */
	frames = bit_read_long(fr->bsbuf, &vbri_offset);

	if(VERBOSE3)
	{
		fprintf(stderr, "Note: VBRI: %lu frames\n", frames);
		fprintf(stderr, "Note: VBRI: %lu bytes\n", bytes);
		fprintf(stderr, "Note: VBRI: delay = %u samples\n", delay);
	}

	fr->vbr = MPG123_VBR;

	if(fr->p.flags & MPG123_IGNORE_STREAMLENGTH)
	{
		if(VERBOSE3)
			fprintf(stderr, "Note: Ignoring VBRI frames/bytes because of MPG123_IGNORE_STREAMLENGTH\n");
	}
	else
	{
		/* VBRI frame count includes the info frame, which will be skipped during playback.
		   Subtract 1 to get the actual number of audio frames. */
		if(frames > 0)
			frames--;

		fr->track_frames = (frames > TRACK_MAX_FRAMES) ? 0 : (int64_t)frames;

		if(fr->rdat.filelen < 1)
			fr->rdat.filelen = (int64_t)bytes + fr->audio_start;

		/* VBRI delay is relative to all frames including the info frame.
		   Since the info frame is skipped, subtract one frame's worth of samples. */
		int64_t adjusted_delay = 0;
		if(delay > fr->spf)
			adjusted_delay = delay - fr->spf;

		/* VBRI delay includes the decoder delay, but enc_delay should only contain
		   the encoder delay (matching LAME tag semantics). */
		int64_t encoder_delay = adjusted_delay >= GAPLESS_DELAY ? adjusted_delay - GAPLESS_DELAY : 0;

		fr->enc_delay = (int)encoder_delay;
		fr->enc_padding = 0; /* VBRI doesn't specify padding */

#ifdef GAPLESS
		if(fr->p.flags & MPG123_GAPLESS)
			INT123_frame_gapless_init(fr, fr->track_frames, encoder_delay, 0);
#endif
	}

	/* switch buffer back */
	fr->bsbuf = fr->bsspace[fr->bsnum] + 512;
	fr->bsnum = (fr->bsnum + 1) & 1;

	return 1;
}
]=])

# add the check_vbri_tag function after check_lame_tag_no label
# (the patch step reruns on the already patched source whenever the patch command changes)
if(NOT FILE_CONTENT MATCHES "check_vbri_tag")
    string(REPLACE
        "check_lame_tag_no:\n\treturn 0;\n}"
        "check_lame_tag_no:\n\treturn 0;\n}\n\n${VBRI_FUNCTION}"
        FILE_CONTENT
        "${FILE_CONTENT}"
    )
endif()

# modify the check_lame_tag call to also check VBRI
string(REPLACE
    "if(fr->hdr.lay == 3 && check_lame_tag(fr) == 1)"
    "if(fr->hdr.lay == 3 && (check_lame_tag(fr) == 1 || check_vbri_tag(fr) == 1))"
    FILE_CONTENT
    "${FILE_CONTENT}"
)

file(WRITE "${FILE_TO_PATCH}" "${FILE_CONTENT}")
message(STATUS "patched ${FILE_TO_PATCH} to add VBRI tag support")

# remove the stray empty #define in the _WIN32 branch of compat.h (upstream typo, breaks MSVC; no-op once fixed upstream)
set(FILE_TO_PATCH "${MPG123_SOURCE_DIR}/src/compat/compat.h")
file(READ "${FILE_TO_PATCH}" FILE_CONTENT)
string(REPLACE
    "#define STDERR_FILENO _fileno(stderr)\n#define\n"
    "#define STDERR_FILENO _fileno(stderr)\n"
    FILE_CONTENT
    "${FILE_CONTENT}"
)
file(WRITE "${FILE_TO_PATCH}" "${FILE_CONTENT}")

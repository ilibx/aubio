/*
  Copyright (C) 2012 Paul Brossier <piem@aubio.org>

  This file is part of aubio.

  aubio is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  aubio is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with aubio.  If not, see <http://www.gnu.org/licenses/>.

*/


#include "config.h"

#ifdef HAVE_SNDFILE

#include <sndfile.h>

#include "aubio_priv.h"
#include "source_sndfile.h"
#include "fvec.h"

#include "temporal/resampler.h"

#define MAX_CHANNELS 6
#define MAX_SIZE 4096
#define MAX_SAMPLES MAX_CHANNELS * MAX_SIZE

struct _aubio_source_sndfile_t {
  uint_t hop_size;
  uint_t samplerate;
  uint_t channels;

  // some data about the file
  char_t *path;
  SNDFILE *handle;
  int input_samplerate;
  int input_channels;
  int input_format;

  // resampling stuff
  smpl_t ratio;
  uint_t input_hop_size;
#ifdef HAVE_SAMPLERATE
  aubio_resampler_t *resampler;
  fvec_t *input_data;
#endif /* HAVE_SAMPLERATE */

  // some temporary memory for sndfile to write at
  uint_t scratch_size;
  smpl_t *scratch_data;
};

aubio_source_sndfile_t * new_aubio_source_sndfile(char_t * path, uint_t samplerate, uint_t hop_size) {
  aubio_source_sndfile_t * s = AUBIO_NEW(aubio_source_sndfile_t);

  if (path == NULL) {
    AUBIO_ERR("Aborted opening null path\n");
    return NULL;
  }

  s->hop_size = hop_size;
  s->samplerate = samplerate;
  s->channels = 1;
  s->path = path;

  // try opening the file, geting the info in sfinfo
  SF_INFO sfinfo;
  AUBIO_MEMSET(&sfinfo, 0, sizeof (sfinfo));
  s->handle = sf_open (s->path, SFM_READ, &sfinfo);

  if (s->handle == NULL) {
    /* show libsndfile err msg */
    AUBIO_ERR("Failed opening %s: %s\n", s->path, sf_strerror (NULL));
    goto beach;
  }	

  /* get input specs */
  s->input_samplerate = sfinfo.samplerate;
  s->input_channels   = sfinfo.channels;
  s->input_format     = sfinfo.format;

  /* compute input block size required before resampling */
  s->ratio = s->samplerate/(float)s->input_samplerate;
  s->input_hop_size = (uint_t)FLOOR(s->hop_size / s->ratio + .5);

  if (s->input_hop_size * s->input_channels > MAX_SAMPLES) {
    AUBIO_ERR("Not able to process more than %d frames of %d channels\n",
        MAX_SAMPLES / s->input_channels, s->input_channels);
    goto beach;
  }

#ifdef HAVE_SAMPLERATE
  s->resampler = NULL;
  s->input_data = NULL;
  if (s->ratio != 1) {
    s->input_data = new_fvec(s->input_hop_size);
    s->resampler = new_aubio_resampler(s->ratio, 4);
    if (s->ratio > 1) {
      // we would need to add a ring buffer for these
      if ( (uint_t)(s->input_hop_size * s->ratio + .5)  != s->hop_size ) {
        AUBIO_ERR("can not upsample from %d to %d\n", s->input_samplerate, s->samplerate);
        goto beach;
      }
      AUBIO_WRN("upsampling %s from %d to % d\n", s->path, s->input_samplerate, s->samplerate);
    }
  }
#else
  if (s->ratio != 1) {
    AUBIO_ERR("aubio was compiled without aubio_resampler\n");
    goto beach;
  }
#endif /* HAVE_SAMPLERATE */

  /* allocate data for de/interleaving reallocated when needed. */
  s->scratch_size = s->input_hop_size * s->input_channels;
  s->scratch_data = AUBIO_ARRAY(float,s->scratch_size);

  return s;

beach:
  AUBIO_ERR("can not read %s at samplerate %dHz with a hop_size of %d\n",
      s->path, s->samplerate, s->hop_size);
  del_aubio_source_sndfile(s);
  return NULL;
}

void aubio_source_sndfile_do(aubio_source_sndfile_t * s, fvec_t * read_data, uint_t * read){
  uint_t i,j, input_channels = s->input_channels;
  /* do actual reading */
  sf_count_t read_samples = sf_read_float (s->handle, s->scratch_data, s->scratch_size);

  smpl_t *data;

#ifdef HAVE_SAMPLERATE
  if (s->ratio != 1) {
    data = s->input_data->data;
  } else
#endif /* HAVE_SAMPLERATE */
  {
    data = read_data->data;
  }

  /* de-interleaving and down-mixing data  */
  for (j = 0; j < read_samples / input_channels; j++) {
    data[j] = 0;
    for (i = 0; i < input_channels; i++) {
      data[j] += (smpl_t)s->scratch_data[input_channels*j+i];
    }
    data[j] /= (smpl_t)input_channels;
  }

#ifdef HAVE_SAMPLERATE
  if (s->resampler) {
    aubio_resampler_do(s->resampler, s->input_data, read_data);
  }
#endif /* HAVE_SAMPLERATE */

  *read = (int)FLOOR(s->ratio * read_samples / input_channels + .5);
}

void del_aubio_source_sndfile(aubio_source_sndfile_t * s){
  if (!s) return;
  if (sf_close(s->handle)) {
    AUBIO_ERR("Error closing file %s: %s", s->path, sf_strerror (NULL));
  }
#ifdef HAVE_SAMPLERATE
  if (s->resampler != NULL) {
    del_aubio_resampler(s->resampler);
  }
  if (s->input_data) {
    del_fvec(s->input_data);
  }
#endif /* HAVE_SAMPLERATE */
  AUBIO_FREE(s->scratch_data);
  AUBIO_FREE(s);
}

#endif /* HAVE_SNDFILE */
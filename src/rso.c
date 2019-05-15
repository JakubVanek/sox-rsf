/* LEGO® MINDSTORMS® NXT RSO and EV3 RSF sound format
 * (c) 2019 Jakub Vanek <linuxtardis@gmail.com>
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or (at
 * your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "sox_i.h"
#include "adpcms.h"
#include "sox.h"
#include "stdio.h"

typedef struct {
    adpcm_io_t adpcm;
    sox_bool is_adpcm;
} priv_t;

#define RSO_MAGIC_U8     0x0100
#define RSO_MAGIC_ADPCM  0x0101
#define RSO_LENGTH_EMPTY 0x0000
#define RSO_MODE_DEFAULT 0x0000

enum {
    LENGTH_ERROR = -1,
    LENGTH_READY = 0,
    LENGTH_MISSING = 1
};

static int lsx_rso_calc_length(sox_format_t *ft, priv_t *priv, uint16_t *for_header) {
    sox_uint64_t samples = ft->olength ? ft->olength : ft->signal.length;
    sox_uint64_t length = 0;

    if (samples) {
        if (priv->is_adpcm) {
            length = (samples + 1) / 2;
        } else {
            length = samples;
        }
        if (length > 65535) {
            return LENGTH_ERROR;
        } else {
            *for_header = (uint16_t)length;
            return LENGTH_READY;
        }
    } else {
        return LENGTH_MISSING;
    }
}

static int lsx_rso_startread(sox_format_t *ft) {
    priv_t *priv = (priv_t*) ft->priv;
    uint16_t hdr_magic = 0;
    uint16_t hdr_bytes = 0;
    uint16_t hdr_rate = 0;
    uint16_t hdr_mode = 0;

    sox_encoding_t sample_type;
    uint64_t sample_count;
    unsigned bits_per_sample;
    int rval;

    if (lsx_readw(ft, &hdr_magic) != SOX_SUCCESS ||
        lsx_readw(ft, &hdr_bytes) != SOX_SUCCESS ||
        lsx_readw(ft, &hdr_rate) != SOX_SUCCESS ||
        lsx_readw(ft, &hdr_mode) != SOX_SUCCESS) {
        return SOX_EOF;
    }

    if (hdr_magic == RSO_MAGIC_U8) {
        priv->is_adpcm = sox_false;

        sample_count = hdr_bytes * 1;
        sample_type = SOX_ENCODING_UNSIGNED;
        bits_per_sample = 8;

    } else if (hdr_magic == RSO_MAGIC_ADPCM) {
        priv->is_adpcm = sox_true;

        sample_count = hdr_bytes * 2;
        sample_type = SOX_ENCODING_IMA_ADPCM;
        bits_per_sample = 4;

    } else {
        lsx_fail_errno(ft, SOX_EHDR, "`%s': unknown sample format", ft->filename);
        return SOX_EOF;
    }

    rval = lsx_check_read_params(ft,
                                 1 /* channels */,
                                 1.0 * hdr_rate,
                                 sample_type,
                                 bits_per_sample,
                                 sample_count,
                                 sox_true /* verify length */);
    if (rval != SOX_SUCCESS) {
        return rval;
    }

    if (priv->is_adpcm) {
        return lsx_adpcm_ima_start(ft, &priv->adpcm);
    } else {
        return lsx_rawstartread(ft);
    }
}

static size_t lsx_rso_read(sox_format_t *ft, sox_sample_t *buf, size_t len) {
    priv_t *priv = (priv_t*) ft->priv;

    if (priv->is_adpcm) {
        return lsx_adpcm_read(ft, &priv->adpcm, buf, len);
    } else {
        return lsx_rawread(ft, buf, len);
    }
}

static int lsx_rso_stopread(sox_format_t *ft) {
    priv_t *priv = (priv_t*) ft->priv;

    if (priv->is_adpcm) {
        return lsx_adpcm_stopread(ft, &priv->adpcm);
    }

    return SOX_SUCCESS;
}


static int lsx_rso_startwrite(sox_format_t *ft) {
    priv_t *priv = (priv_t*) ft->priv;
    uint16_t magic;

    if (ft->encoding.encoding == SOX_ENCODING_UNSIGNED) {
        priv->is_adpcm = sox_false;
        magic = RSO_MAGIC_U8;

    } else if (ft->encoding.encoding == SOX_ENCODING_IMA_ADPCM) {
        priv->is_adpcm = sox_true;
        magic = RSO_MAGIC_ADPCM;

    } else {
        lsx_fail_errno(ft, SOX_EFMT, "`%s': sample encoding is not supported", ft->filename);
        return SOX_EOF;
    }

    if (lsx_writew(ft, magic)                     != SOX_SUCCESS ||
        lsx_writew(ft, RSO_LENGTH_EMPTY)          != SOX_SUCCESS ||
        lsx_writew(ft, (uint16_t)ft->signal.rate) != SOX_SUCCESS ||
        lsx_writew(ft, RSO_MODE_DEFAULT)          != SOX_SUCCESS) {
        return SOX_EOF;
    }

    ft->data_start = lsx_tell(ft);

    if (priv->is_adpcm) {
        return lsx_adpcm_ima_start(ft, &priv->adpcm);
    } else {
        return lsx_rawstartwrite(ft);
    }
}

static size_t lsx_rso_write(sox_format_t *ft, const sox_sample_t *buf, size_t len) {
    priv_t *priv = (priv_t*) ft->priv;

    if (priv->is_adpcm) {
        return lsx_adpcm_write(ft, &priv->adpcm, buf, len);
    } else {
        return lsx_rawwrite(ft, buf, len);
    }
}

static int lsx_rso_stopwrite(sox_format_t *ft) {
    priv_t *priv = (priv_t*) ft->priv;
    off_t target = (off_t)ft->data_start - 6;
    uint16_t hdr_bytes = 0;

    if (priv->is_adpcm) {
        if (lsx_adpcm_stopwrite(ft, &priv->adpcm) != SOX_SUCCESS) {
            lsx_warn("IMA ADPCM stop failed");
            return SOX_EOF;
        }
    }

    if (ft->seekable) {
        int rval = lsx_rso_calc_length(ft, priv, &hdr_bytes);

        if (rval == LENGTH_ERROR) {
            lsx_warn("`%s': output file is too long, length header will be truncated", ft->filename);
            hdr_bytes = 0xFFFF;

        } else if (rval == LENGTH_MISSING) {
            lsx_warn("`%s': file length not available, file header will indicate zero length", ft->filename);
            hdr_bytes = 0x0000;
        }

        if (lsx_seeki(ft, target, SEEK_SET)  != SOX_SUCCESS ||
            lsx_writew(ft, hdr_bytes)        != SOX_SUCCESS) {
            lsx_warn("`%s': seek or header write failed", ft->filename);
            return SOX_EOF;
        }
    } else {
        lsx_warn("`%s': seeking is not possible, the output file will have broken length header", ft->filename);
    }

    return SOX_SUCCESS;
}

LSX_FORMAT_HANDLER(rso)
{
  static char const * const names[] = {"rso", "rsf", NULL};
  static unsigned const write_encodings[] = {
      SOX_ENCODING_UNSIGNED, 8, 0,
//    SOX_ENCODING_IMA_ADPCM, 4, 0, // playback broken on EV3?
      0};
  static sox_rate_t const sample_rates[] = {
      8000.0, 0.0
  };
  static sox_format_handler_t handler = {SOX_LIB_VERSION_CODE,
    "LEGO(R) MINDSTORMS(R) NXT RSO / EV3 RSF", names,
    SOX_FILE_MONO | SOX_FILE_BIG_END,
    lsx_rso_startread, lsx_rso_read, lsx_rso_stopread,
    lsx_rso_startwrite, lsx_rso_write, lsx_rso_stopwrite,
    lsx_rawseek, write_encodings, sample_rates, sizeof(priv_t)
  };
  return &handler;
}

/*
  Copyright (c) 2017 Clerk Ma

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.
*/

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#include "md5.h"

typedef struct {
  uint32_t tag;
  uint32_t chemSum;
  uint32_t offset;
  uint32_t length;
} ot_table_record;

typedef struct {
  uint32_t sfntVersion;
  uint16_t numTables;
  uint16_t searchRange;
  uint16_t entrySelector;
  uint16_t rangeShift;
} ot_offset_table;

typedef struct {
  uint32_t len;
  uint8_t * data;
} ot_data;

typedef struct {
  md5_byte_t digest[16];
  uint8_t * ref;
  uint32_t length;
  uint32_t offset;
} ot_table_data;

typedef struct {
  ot_table_data * tbl;
  uint32_t count;
  uint32_t used;
  uint32_t offset;
} ot_table_cache;

static uint16_t parse_u16 (uint8_t * s)
{
  return (*s << 8) | (*(s + 1));
}

static uint32_t parse_u32 (uint8_t * s)
{
  return (*s << 24) | (*(s + 1) << 16) | (*(s + 2) << 8) | (*(s + 3));
}

static void write_u16 (FILE * f, uint16_t n)
{
  uint8_t b0 = ((n >> 010) & 0xff);
  uint8_t b1 = (n          & 0xff);

  fputc(b0, f);
  fputc(b1, f);
}

static void write_u32 (FILE * f, uint32_t n)
{
  uint8_t b0 = ((n >> 030) & 0xff);
  uint8_t b1 = ((n >> 020) & 0xff);
  uint8_t b2 = ((n >> 010) & 0xff);
  uint8_t b3 = (n          & 0xff);

  fputc(b0, f);
  fputc(b1, f);
  fputc(b2, f);
  fputc(b3, f);
}

static ot_data * read_opentype_font (const char * name)
{
  FILE * f = fopen(name, "rb");
  uint32_t len = 0;
  ot_data * otf = calloc(1, sizeof(ot_data));
  fseek(f, 0, SEEK_END);
  len = ftell(f);
  otf->len = len;
  otf->data = calloc(len, sizeof(uint8_t));
  fseek(f, 0, SEEK_SET);
  fread(otf->data, len, sizeof(uint8_t), f);
  fclose(f);

  return otf;
}

static ot_table_cache * collection_cache_init (uint32_t table_count, uint32_t offset_body)
{
  ot_table_cache * cache = calloc(1, sizeof(ot_table_cache));
  cache->tbl = calloc(table_count, sizeof(ot_table_data));
  cache->count = table_count;
  cache->used = 0;
  cache->offset = offset_body;

  return cache;
}

static void collection_cache_fini (ot_table_cache * cache)
{
  if (cache)
  {
    if (cache->tbl)
      free(cache->tbl);
    free(cache);
  }
}

static uint32_t cache_table_data (ot_table_cache * cache, uint8_t * ref, uint32_t length)
{
  md5_state_t state;
  md5_byte_t digest[16];
  int32_t cached_index = -1;

  md5_init(&state);
  md5_append(&state, (const md5_byte_t *) ref, length);
  md5_finish(&state, digest);

  for (uint32_t i = 0; i < cache->used; i++)
  {
    if (memcmp(digest, cache->tbl[i].digest, 16) == 0)
      cached_index = i;
  }

  if (cached_index > -1)
  {
    return cache->tbl[cached_index].offset;
  }
  else
  {
    cache->tbl[cache->used].length = length;
    cache->tbl[cache->used].ref = ref;
    memcpy(cache->tbl[cache->used].digest, digest, 16);
    cache->tbl[cache->used].offset = cache->offset;
    cache->offset += length;
    cache->used += 1;
    return cache->tbl[cache->used - 1].offset;
  }
}

int main (int argc, char ** argv)
{
  if (argc < 3)
  {
    printf("Usage: glue-fonts output.ttc input1.ttf intput2.ttf ...\n");
  }
  else
  {
    uint32_t input_count = argc - 2;
    ot_data ** input_buffer = calloc(input_count, sizeof(ot_data *));
    uint32_t offset_body = 0;
    uint32_t table_count = 0;
    uint32_t table_used = 0;

    for (uint32_t i = 0; i < input_count; i++)
    {
      input_buffer[i] = read_opentype_font(argv[2 + i]);
    }

    FILE * f = fopen(argv[1], "wb");
    fprintf(f, "ttcf");
    write_u16(f, 1);
    write_u16(f, 0);
    write_u32(f, input_count);

    offset_body = 12 + input_count * 4;
    for (uint32_t i = 0; i < input_count; i++)
    {
      write_u32(f, offset_body);
      offset_body += parse_u16(input_buffer[i]->data + 4) * 16 + 12;
      table_count += parse_u16(input_buffer[i]->data + 4);
    }

    ot_table_cache * cache = collection_cache_init(table_count, offset_body);

    for (uint32_t i = 0; i < input_count; i++)
    {
      fwrite(input_buffer[i]->data, sizeof(uint8_t), 12, f);

      for (uint32_t j = 0; j < parse_u16(input_buffer[i]->data + 4); j++)
      {
        fwrite(input_buffer[i]->data + 12 + j * 16, sizeof(uint8_t), 8, f);
        write_u32(f,
          cache_table_data(cache,
            input_buffer[i]->data + parse_u32(input_buffer[i]->data + 12 + j * 16 + 8),
            parse_u32(input_buffer[i]->data + 12 + j * 16 + 12)));
        fwrite(input_buffer[i]->data + 12 + j * 16 + 12, sizeof(uint8_t), 4, f);
      }
    }

    for (uint32_t i = 0; i < cache->used; i++)
    {
      fwrite(cache->tbl[i].ref, sizeof(uint8_t), cache->tbl[i].length, f);
    }

    collection_cache_fini(cache);
    
    for (uint32_t i = 0; i < input_count; i++)
    {
      if (input_buffer[i]->data)
        free(input_buffer[i]->data);
      free(input_buffer[i]);
    }
    
    if (input_buffer)
      free(input_buffer);

    fclose(f);
  }
}

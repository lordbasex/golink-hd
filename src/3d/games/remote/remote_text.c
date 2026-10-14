/* Copyright (c) 2026 Federico Pereira <lord.basex@gmail.com> */
/*
 * The texts of TELETRABAJO: CÁMARA APAGADA, one row each in English,
 * Spanish and Portuguese (hd_lang). Spanish is the reference: it is written
 * first and the others follow it. Translators touch only this file.
 */
#include "remote.h"

static const char *const texts[RT_COUNT][LANGS] = {
   [RT_TITLE] = { "REMOTE WORK: CAMERA OFF", "TELETRABAJO: CÁMARA APAGADA", "TELETRABALHO: CÂMERA DESLIGADA" },
   [RT_HINT_WALK] = { "Stick: walk   X/Y: run   L/R: camera", "Stick: caminar   X/Y: correr   L/R: cámara", "Analógico: andar   X/Y: correr   L/R: câmera" },
};

const char *remote_text(int32_t id)
{
   if (id < 0 || id >= RT_COUNT)
      return "";
   return texts[id][hd_clamp(hd_lang, 0, LANGS - 1)];
}

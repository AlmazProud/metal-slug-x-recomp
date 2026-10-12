/* Frontend language: English (default) or Spanish.
 *
 * Every user-visible frontend string is written in English and passed through T(); with Spanish
 * selected, T() returns its translation from the table in lang.c (or the English text if it has
 * none). Format strings are translated before formatting: snprintf(b, n, T("< Slot %d >"), x).
 */
#ifndef LANG_H
#define LANG_H

enum { LANG_EN, LANG_ES, LANG_COUNT };

extern int lang;                         /* LANG_*: the active language */

void lang_set(int l);
const char *tr(const char *en);
#define T(s) tr(s)

#endif

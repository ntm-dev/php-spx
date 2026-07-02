/* SPX - A simple profiler for PHP
 * Copyright (C) 2017-2025 Sylvain Lassaut <NoiseByNorthwest@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef ZTS
#   include <pthread.h>
#endif

#include <arpa/inet.h>
#include <sys/file.h>

#include "spx_utils.h"
#include "spx_php.h"

int spx_utils_ip_match(const char * ip_address_str, const char * target)
{
    if (
        strcmp(target, "*") == 0 ||
        strcmp(target, ip_address_str) == 0
    ) {
        return 1;
    }

    // subnet handling

    const char * slash_ptr = strchr(target, '/');
    if (slash_ptr == NULL) {
        return 0;
    }

    const size_t slash_pos = slash_ptr - target;
    if (! (7 <= slash_pos && slash_pos <= 15)) {
        return 0;
    }

    const size_t target_suffix_len = strlen(slash_ptr);
    if (! (2 <= target_suffix_len && target_suffix_len <= 3)) {
        return 0;
    }

    char target_ip_address_str[32];
    strncpy(target_ip_address_str, target, sizeof target_ip_address_str);
    target_ip_address_str[slash_pos] = 0;

    const in_addr_t target_ip_address = inet_addr(target_ip_address_str);
    if (target_ip_address == INADDR_NONE) {
        return 0;
    }

    char target_mask_str[32];
    snprintf(target_mask_str, sizeof target_mask_str, "%s", slash_ptr + 1);
    const long target_mask_bits = strtol(target_mask_str, NULL, 10);

    if (! (1 <= target_mask_bits && target_mask_bits <= 31)) {
        return 0;
    }

    const in_addr_t target_mask = (~0) << (32 - target_mask_bits);

    const in_addr_t ip_address = inet_addr(ip_address_str);
    if (ip_address == INADDR_NONE) {
        return 0;
    }

    if ((ntohl(ip_address) & target_mask) == (ntohl(target_ip_address) & target_mask)) {
        return 1;
    }

    return 0;
}

int spx_utils_wildcard_match(const char * str, const char * pattern)
{
    const char * s = str;
    const char * p = pattern;
    const char * star_p = NULL;
    const char * star_s = NULL;

    while (*s) {
        if (*p == '*') {
            star_p = p++;
            star_s = s;
        } else if (*p == *s) {
            p++;
            s++;
        } else if (star_p) {
            p = star_p + 1;
            s = ++star_s;
        } else {
            return 0;
        }
    }

    while (*p == '*') {
        p++;
    }

    return *p == 0;
}

static char * chomp(char * line)
{
    size_t len = strlen(line);
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
        line[--len] = 0;
    }

    return line;
}

int spx_utils_file_list_lines(
    const char * file_path,
    void (* callback) (const char * line, void * arg),
    void * arg
) {
    FILE * fp = fopen(file_path, "r");
    if (!fp) {
        /* the file may simply not exist yet -> empty list, not an error */
        return 0;
    }

    if (flock(fileno(fp), LOCK_SH) != 0) {
        fclose(fp);

        return -1;
    }

    char line[SPX_UTILS_FILE_LINE_MAX_LEN];
    while (fgets(line, sizeof(line), fp) != NULL) {
        chomp(line);

        if (line[0] != 0) {
            callback(line, arg);
        }
    }

    flock(fileno(fp), LOCK_UN);
    fclose(fp);

    return 0;
}

int spx_utils_file_append_line_unique(const char * file_path, const char * line)
{
    FILE * fp = fopen(file_path, "a+");
    if (!fp) {
        return -1;
    }

    if (flock(fileno(fp), LOCK_EX) != 0) {
        fclose(fp);

        return -1;
    }

    rewind(fp);

    char existing_line[SPX_UTILS_FILE_LINE_MAX_LEN];
    while (fgets(existing_line, sizeof(existing_line), fp) != NULL) {
        if (0 == strcmp(chomp(existing_line), line)) {
            /* already present -> nothing to do */
            flock(fileno(fp), LOCK_UN);
            fclose(fp);

            return 0;
        }
    }

    fseek(fp, 0, SEEK_END);
    fprintf(fp, "%s\n", line);

    flock(fileno(fp), LOCK_UN);
    fclose(fp);

    return 0;
}

int spx_utils_file_remove_line(const char * file_path, const char * line)
{
    FILE * fp = fopen(file_path, "r+");
    if (!fp) {
        /* nothing to remove */
        return 0;
    }

    if (flock(fileno(fp), LOCK_EX) != 0) {
        fclose(fp);

        return -1;
    }

    char tmp_file_path[PATH_MAX];
    snprintf(tmp_file_path, sizeof(tmp_file_path), "%s.tmp", file_path);

    FILE * tmp_fp = fopen(tmp_file_path, "w");
    if (!tmp_fp) {
        flock(fileno(fp), LOCK_UN);
        fclose(fp);

        return -1;
    }

    char existing_line[SPX_UTILS_FILE_LINE_MAX_LEN];
    while (fgets(existing_line, sizeof(existing_line), fp) != NULL) {
        chomp(existing_line);

        if (existing_line[0] != 0 && 0 != strcmp(existing_line, line)) {
            fprintf(tmp_fp, "%s\n", existing_line);
        }
    }

    fclose(tmp_fp);

    const int renamed = (0 == rename(tmp_file_path, file_path));

    flock(fileno(fp), LOCK_UN);
    fclose(fp);

    return renamed ? 0 : -1;
}

char * spx_utils_resolve_confined_file_absolute_path(
    const char * root_dir,
    const char * relative_path,
    const char * suffix,
    char * dst,
    size_t size
) {
    if (size < PATH_MAX) {
        spx_utils_die("size < PATH_MAX");
    }

    char absolute_file_path[PATH_MAX];

    snprintf(
        absolute_file_path,
        sizeof(absolute_file_path),
        "%s%s%s",
        root_dir,
        relative_path,
        suffix == NULL ? "" : suffix
    );

    if (realpath(absolute_file_path, dst) == NULL) {
        return NULL;
    }

    char root_dir_real_path[PATH_MAX];
    if (realpath(root_dir, root_dir_real_path) == NULL) {
        return NULL;
    }

    char expected_path_prefix[PATH_MAX + 1];
    snprintf(
        expected_path_prefix,
        sizeof(expected_path_prefix),
        "%s/",
        root_dir_real_path
    );

    if (! spx_utils_str_starts_with(dst, expected_path_prefix)) {
        return NULL;
    }

    return dst;
}

char * spx_utils_json_escape(char * dst, const char * src, size_t limit)
{
    size_t i = 0;
    while (*src) {
        if (i >= limit) {
            goto limit_reached;
        }

        char escaped_char = 0;

        switch (*src) {
            case '\\':
            case '"':
            case '/':
                escaped_char = *src;

                break;

            case '\b':
                escaped_char = 'b';

                break;

            case '\f':
                escaped_char = 'f';

                break;

            case '\n':
                escaped_char = 'n';

                break;

            case '\r':
                escaped_char = 'r';

                break;

            case '\t':
                escaped_char = 't';

                break;
        }

        if (escaped_char != 0) {
            dst[i++] = '\\';

            if (i >= limit) {
                goto limit_reached;
            }

            dst[i++] = escaped_char;
        } else {
            dst[i++] = *src;
        }

        src++;
    }

    if (i >= limit) {
        goto limit_reached;
    }

    dst[i] = 0;

    return dst;

limit_reached:
    spx_utils_die("The provided buffer is too small to contain the escaped JSON string");

    /* unreachable */
    return NULL;
}

int spx_utils_str_starts_with(const char * str, const char * prefix)
{
    return 0 == strncmp(str, prefix, strlen(prefix));
}

int spx_utils_str_ends_with(const char * str, const char * suffix)
{
    const size_t str_len = strlen(str);
    const size_t suffix_len = strlen(suffix);

    if (str_len < suffix_len) {
        return 0;
    }

    if (strcmp(str + str_len - suffix_len, suffix) == 0) {
        return 1;
    }

    return 0;
}

void spx_utils_die_(const char * msg, const char * file, size_t line)
{
    fprintf(stderr, "SPX Fatal error at %s:%zu - %s\n", file, line, msg);

#ifdef ZTS
    pthread_exit(NULL);
#else
    exit(EXIT_FAILURE);
#endif
}

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <uthash.h>

#include "util.h"

struct str_htab {
	char *str;
	UT_hash_handle hh;
};

static int drv_read_unwrapped(const char *drv_path, struct str_htab **str_htab);
static int str_htab_new(struct str_htab **str_htab, const char *key);
static void str_htab_free(struct str_htab *sh);

int drv_read(const char *drv_path)
{
	int ret;
	struct str_htab *sh = NULL, *i, *tmp;

	ret = drv_read_unwrapped(drv_path, &sh);
	HASH_ITER(hh, sh, i, tmp) {
		HASH_DEL(sh, i);
		str_htab_free(i);
	}

	return ret;
}

static void str_htab_free(struct str_htab *sh)
{
	free(sh->str);
	free(sh);
}

static int str_htab_new(struct str_htab **str_htab, const char *key)
{
	int ret = 0;
	struct str_htab *sh;

	sh = malloc(sizeof(*sh));
	if (sh == NULL) {
		print_err("%s", strerror(errno));
		return -errno;
	}

	sh->str = strdup(key);
	if (sh->str == NULL) {
		print_err("%s", strerror(errno));
		ret = -errno;
		goto out_free_sh;
	}

out_free_sh:
	if (ret < 0)
		free(sh);
	else
		*str_htab = sh;

	return ret;
}

/* shamelessly copied from  nix::parseDerivation, https://github.com/NixOS/nix
 * should be replaced with the Nix libstore C API when it's complete
 * */
static int drv_read_unwrapped(const char *drv_path, struct str_htab **str_htab)
{
	FILE *fp;
	int ret;
	size_t len;
	struct str_htab *sh = NULL;
	char *line = NULL, *str, *end, *target;

	fp = fopen(drv_path, "r");
	if (fp == NULL) {
		print_err("%s: %s\n", strerror(errno), drv_path);
		return -errno;
	}

	errno = 0;
	ret = getline(&line, &len, fp);
	fclose(fp);
	if (errno != 0) {
		print_err("%s", strerror(errno));
		return -errno;
	}
	str = line;

	/* skip outputs */
	str = strchr(str, '[');
	if (str == NULL) {
		goto out_free_line;
		ret = -EPERM;
	}

	/* parse inputDrvs */
	str = strstr(str + 1, ",[");
	if (str == NULL) {
		goto out_free_line;
		ret = -EPERM;
	}
	end = strstr(str, "],");
	if (str == NULL) {
		goto out_free_line;
		ret = -EPERM;
	}
	end[1] = '\0';


	while (1) {
		target = strstr(str, "(\"");
		if (target == NULL) {
			goto out_free_line;
			ret = -EPERM;
		}
		target += 2;

		end = strchr(target, '"');
		if (end == NULL) {
			goto out_free_line;
			ret = -EPERM;
		}
		*end = '\0';
		str = end + 1;

		HASH_FIND_STR(*str_htab, target, sh);
		if (sh == NULL) {
			ret = str_htab_new(&sh, target);
			if (ret < 0)
				goto out_free_line;

			HASH_ADD_STR(*str_htab, str, sh);
		} else {
			continue;
		}

		puts(target);
		drv_read_unwrapped(target, str_htab);
	}

out_free_line:
	free(line);

	return ret;
}

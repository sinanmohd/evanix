#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uthash.h>

#include "derivation.h"
#include "util.h"

struct str_htab {
	char *str;
	UT_hash_handle hh;
};

static int drv_read_unwrapped(struct cache *cache, const char *drv_path,
			      struct str_htab **str_htab);
static int str_htab_new(struct str_htab **str_htab, const char *key);
static void str_htab_free(struct str_htab *sh);
static int drv_outputs_read(struct cache *cache, char *str, char **output_end,
			    const char *drv_path);
static int drv_inputdrvs_read(struct cache *cache, char *str,
			      char **inputdrvs_end, struct str_htab **str_htab,
			      const char *drv_path);

int drv_read(struct cache *cache, const char *drv_path)
{
	int ret;
	struct str_htab *sh = NULL, *i, *tmp;

	ret = drv_read_unwrapped(cache, drv_path, &sh);
	HASH_ITER (hh, sh, i, tmp) {
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

static int drv_outputs_read(struct cache *cache, char *str, char **output_end,
			    const char *drv_path)
{
	int ret = 0;
	cache_state_t cache_state = CACHE_LOCAL;
	char *end, *output_name, *output_path, *list_end;

	str = strchr(str, '[');
	if (str == NULL) {
		ret = -EPERM;
		goto out_print_err;
	}
	list_end = strchr(str, ']');
	if (list_end == NULL) {
		ret = -EPERM;
		goto out_print_err;
	}

	while (1) {
		str = strstr(str + 1, "(\"");
		if (str == NULL || str > list_end)
			break;

		output_name = str + 2;
		end = strstr(output_name, "\",\"");
		if (end == NULL) {
			ret = -EPERM;
			goto out_print_err;
		}
		*end = '\0';

		output_path = end + 3;
		end = strstr(output_path, "\"");
		if (end == NULL) {
			ret = -EPERM;
			goto out_print_err;
		}
		*end = '\0';
		str = end + 1;

		ret = cache_state_read(cache, output_path);
		if (ret < 0)
			goto out_print_err;

		if (ret == CACHE_NONE) {
			cache_state = ret;
			goto out_print_err;
		} else if (ret == CACHE_REMOTE) {
			cache_state = ret;
		}
	}

out_print_err:
	if (ret < 0) {
		print_err("Failed to read inputDrvs from %s", drv_path);
		return ret;
	} else {
		*output_end = list_end;
		return cache_state;
	}
}

static int drv_inputdrvs_read(struct cache *cache, char *str,
			      char **inputdrvs_end, struct str_htab **str_htab,
			      const char *drv_path)
{
	char *end, *target, *list_end;
	int ret = 0;
	int recused = false;
	struct str_htab *sh = NULL;

	str = strchr(str, '[');
	if (str == NULL) {
		ret = -EPERM;
		goto out_print_err;
	}
	list_end = strstr(str, "],");
	if (list_end == NULL) {
		ret = -EPERM;
		goto out_print_err;
	}

	while (1) {
		target = strstr(str, "(\"");
		if (target == NULL || target > list_end)
			break;
		target += 2;

		end = strchr(target, '"');
		if (end == NULL) {
			ret = -EPERM;
			goto out_print_err;
		}
		*end = '\0';
		str = end + 1;

		HASH_FIND_STR(*str_htab, target, sh);
		if (sh == NULL) {
			ret = str_htab_new(&sh, target);
			if (ret < 0)
				goto out_print_err;

			HASH_ADD_STR(*str_htab, str, sh);
		} else {
			continue;
		}

		ret = drv_read_unwrapped(cache, target, str_htab);
		if (ret < 0)
			goto out_print_err;
		else if (ret == CACHE_NONE) {
			printf("recursing %s\n", target);
			recused = true;
		} else {
			printf("skipping %s\n", target);
		}
	}

	if (recused)
		printf("---> rec deps from %s\n", drv_path);
	else
		printf("---> all deps skipped from %s\n", drv_path);

out_print_err:
	if (ret < 0)
		print_err("Failed to read inputDrvs from %s", drv_path);
	else
		*inputdrvs_end = list_end;

	return ret;
}

/* shamelessly copied from  nix::parseDerivation, https://github.com/NixOS/nix
 * should be replaced with the Nix libstore C API when it's complete
 * */
static int drv_read_unwrapped(struct cache *cache, const char *drv_path,
			      struct str_htab **str_htab)
{
	FILE *fp;
	int ret;
	size_t len;
	char *str;
	char *line = NULL;

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

	ret = drv_outputs_read(cache, str, &str, drv_path);
	if (ret < 0)
		goto out_free_line;
	else if (ret == CACHE_REMOTE || ret == CACHE_LOCAL)
		goto out_free_line;

	ret = drv_inputdrvs_read(cache, str, &str, str_htab, drv_path);
	if (ret < 0)
		goto out_free_line;
	else
		ret = CACHE_NONE;

out_free_line:
	free(line);

	return ret;
}

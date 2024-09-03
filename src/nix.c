#include <errno.h>
#include <string.h>

#include "nix.h"
#include "util.h"

void _nix_get_string_strdup(const char *str, unsigned n, void *user_data)
{
	char **s = user_data;

	*s = strndup(str, n);
	if (*s == NULL)
		print_err("%s", strerror(errno));
}

int _nix_init(nix_c_context **nix_ctx)
{
	nix_err nix_ret;
	nix_c_context *nc;
	int ret = 0;

	nc = nix_c_context_create();
	if (nix_ctx == NULL) {
		print_err("%s", "Failed to create nix context");
		return -EPERM;
	}

	nix_ret = nix_libstore_init(nc);
	if (nix_ret != NIX_OK) {
		print_err("%s", nix_err_msg(NULL, nc, NULL));
		ret = -EPERM;
		goto out_free_nc;
	}

out_free_nc:
	if (ret < 0)
		nix_c_context_free(nc);
	else
		*nix_ctx = nc;

	return ret;
}

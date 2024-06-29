#include <Python.h>

static PyMethodDef methods[] = {
	{NULL, NULL, 0, NULL},
};

static struct PyModuleDef module = {
	PyModuleDef_HEAD_INIT, "evanix", NULL, -1, methods,
};

PyMODINIT_FUNC PyInit_evanix(void) { return PyModule_Create(&module); }

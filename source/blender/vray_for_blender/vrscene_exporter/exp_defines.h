/*
 * ***** BEGIN GPL LICENSE BLOCK *****
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * The Original Code is Copyright (C) 2010 Blender Foundation.
 * All rights reserved.
 *
 * Contributor(s): Andrei Izrantcev <andrei.izrantcev@chaosgroup.com>
 *
 * ***** END GPL LICENSE BLOCK *****
 */

#ifndef CGR_EXP_DEFINES_H
#define CGR_EXP_DEFINES_H

#define COPY_VECTOR_3_3(a, b) \
	a[0] = b[0];\
	a[1] = b[1];\
	a[2] = b[2];

#define HEX(x) htonl(*(int*)&(x))

#define PYTHON_PRINT_BUF char buf[2048]

#define PYTHON_PRINT(pyObject, buf) \
	PyObject_CallMethod(pyObject, (char*)"write", (char*)"s", buf);

#define WRITE_PYOBJECT_BUF(pyObject) \
	PyObject_CallMethod(pyObject, (char*)"write", (char*)"s", buf);

#define PYTHON_PRINTF(pyObject, ...) \
	sprintf(buf, __VA_ARGS__); \
	WRITE_PYOBJECT_BUF(pyObject);

#define WRITE_PYOBJECT(pyObject, ...) \
	sprintf(buf, __VA_ARGS__); \
	WRITE_PYOBJECT_BUF(pyObject);

#define WRITE_PYOBJECT_HEX_VALUE(pyObject, v) \
	sprintf(buf, "%08X", HEX(v)); \
	WRITE_PYOBJECT_BUF(pyObject);

#define WRITE_PYOBJECT_HEX_VECTOR(pyObject, v) \
	sprintf(buf, "%08X%08X%08X", HEX(v[0]), HEX(v[1]), HEX(v[2])); \
	WRITE_PYOBJECT_BUF(pyObject);

#define WRITE_PYOBJECT_TRANSFORM(pyObject, m) \
	char tmBuf[129]; \
	GetTransformHex(m, tmBuf); \
	sprintf(buf, "TransformHex(\"%s\")", tmBuf);\
	WRITE_PYOBJECT_BUF(pyObject);

#endif // CGR_EXP_DEFINES_H

#include "Types.hpp"

#include "BufferFormat.hpp"

PyObject * MGLContext_vertex_array(MGLContext * self, PyObject * args) {
	MGLProgram * program;
	PyObject * content;
	MGLBuffer * index_buffer;

	int args_ok = PyArg_ParseTuple(
		args,
		"O!OO",
		&MGLProgram_Type,
		&program,
		&content,
		&index_buffer
	);

	if (!args_ok) {
		return 0;
	}

	if (program->context != self) {
		MGLError_Set("the program belongs to a different context");
		return 0;
	}

	if (index_buffer != (MGLBuffer *)Py_None && index_buffer->context != self) {
		MGLError_Set("the index_buffer belongs to a different context");
		return 0;
	}

	int content_len = (int)PyTuple_GET_SIZE(content);

	if (!content_len) {
		MGLError_Set("the content must not be emtpy");
		return 0;
	}

	for (int i = 0; i < content_len; ++i) {
		PyObject * tuple = PyTuple_GET_ITEM(content, i);
		PyObject * buffer = PyTuple_GET_ITEM(tuple, 0);
		PyObject * format = PyTuple_GET_ITEM(tuple, 1);

		if (Py_TYPE(buffer) != &MGLBuffer_Type) {
			MGLError_Set("content[%d][0] must be a Buffer not %s", i, Py_TYPE(buffer)->tp_name);
			return 0;
		}

		if (Py_TYPE(format) != &PyUnicode_Type) {
			MGLError_Set("content[%d][1] must be a string not %s", i, Py_TYPE(format)->tp_name);
			return 0;
		}

		if (((MGLBuffer *)buffer)->context != self) {
			MGLError_Set("content[%d][0] belongs to a different context", i);
			return 0;
		}

		FormatIterator it = FormatIterator(PyUnicode_AsUTF8(format));
		FormatInfo format_info = it.info();

		if (!format_info.valid) {
			MGLError_Set("content[%d][1] is an invalid format", i);
			return 0;
		}

		if (i == 0 && format_info.divisor) {
			MGLError_Set("the first vertex attribute must not be a per instance attribute");
			return 0;
		}

		int attributes_len = (int)PyTuple_GET_SIZE(tuple) - 2;

		if (!attributes_len) {
			MGLError_Set("content[%d][2] must not be empty", i);
			return 0;
		}

		if (attributes_len != format_info.nodes) {
			MGLError_Set("content[%d][1] and content[%d][2] size mismatch %d != %d", i, i, format_info.nodes, attributes_len);
			return 0;
		}

		for (int j = 0; j < attributes_len; ++j) {
			FormatNode * node = it.next();

			while (!node->type) {
				node = it.next();
			}

			MGLAttribute * attribute = (MGLAttribute *)PyTuple_GET_ITEM(tuple, j + 2);

			if (Py_TYPE(attribute) != &MGLAttribute_Type) {
				MGLError_Set("content[%d][%d] must be an attribute not %s", i, j + 2, Py_TYPE(attribute)->tp_name);
				return 0;
			}

			if (node->count % attribute->rows_length) {
				MGLError_Set("invalid format");
				return 0;
			}
		}
	}

	if (index_buffer != (MGLBuffer *)Py_None && Py_TYPE(index_buffer) != &MGLBuffer_Type) {
		MGLError_Set("the index_buffer must be a Buffer not %s", Py_TYPE(index_buffer)->tp_name);
		return 0;
	}

	const GLMethods & gl = self->gl;

	MGLVertexArray * array = (MGLVertexArray *)MGLVertexArray_Type.tp_alloc(&MGLVertexArray_Type, 0);

	Py_INCREF(program);
	array->program = program;

	array->vertex_array_obj = 0;
	gl.GenVertexArrays(1, (GLuint *)&array->vertex_array_obj);

	if (!array->vertex_array_obj) {
		MGLError_Set("cannot create vertex array");
		Py_DECREF(array);
		return 0;
	}

	gl.BindVertexArray(array->vertex_array_obj);

	Py_INCREF(index_buffer);
	array->index_buffer = index_buffer;

	if (index_buffer != (MGLBuffer *)Py_None) {
		array->num_vertices = (int)(index_buffer->size / 4);
		gl.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, index_buffer->buffer_obj);
	} else {
		array->num_vertices = -1;
	}

	for (int i = 0; i < content_len; ++i) {
		PyObject * tuple = PyTuple_GET_ITEM(content, i);

		MGLBuffer * buffer = (MGLBuffer *)PyTuple_GET_ITEM(tuple, 0);
		const char * format = PyUnicode_AsUTF8(PyTuple_GET_ITEM(tuple, 1));

		FormatIterator it = FormatIterator(format);
		FormatInfo format_info = it.info();

		int buf_vertices = (int)(buffer->size / format_info.size);

		if (!format_info.divisor && array->index_buffer == (MGLBuffer *)Py_None && (!i || array->num_vertices > buf_vertices)) {
			array->num_vertices = buf_vertices;
		}

		gl.BindBuffer(GL_ARRAY_BUFFER, buffer->buffer_obj);

		char * ptr = 0;

		int attributes_len = (int)PyTuple_GET_SIZE(tuple) - 2;

		for (int j = 0; j < attributes_len; ++j) {
			FormatNode * node = it.next();

			while (!node->type) {
				ptr += node->size;
				node = it.next();
			}

			MGLAttribute * attribute = (MGLAttribute *)PyTuple_GET_ITEM(tuple, j + 2);

			for (int r = 0; r < attribute->rows_length; ++r) {
				int location = attribute->location + r;
				int count = node->count / attribute->rows_length;

				if (attribute->normalizable) {
					((gl_attribute_normal_ptr_proc)attribute->gl_attrib_ptr_proc)(location, count, node->type, node->normalize, format_info.size, ptr);
				} else {
					((gl_attribute_ptr_proc)attribute->gl_attrib_ptr_proc)(location, count, node->type, format_info.size, ptr);
				}

				gl.VertexAttribDivisor(location, format_info.divisor);

				gl.EnableVertexAttribArray(location);

				ptr += node->size / attribute->rows_length;
			}
		}
	}

	Py_INCREF(self);
	array->context = self;

	MGLVertexArray_Complete(array);

	Py_INCREF(array);

	PyObject * result = PyTuple_New(2);
	PyTuple_SET_ITEM(result, 0, (PyObject *)array);
	PyTuple_SET_ITEM(result, 1, PyLong_FromLong(array->vertex_array_obj));
	return result;
}

PyObject * MGLVertexArray_tp_new(PyTypeObject * type, PyObject * args, PyObject * kwargs) {
	MGLVertexArray * self = (MGLVertexArray *)type->tp_alloc(type, 0);

	if (self) {
	}

	return (PyObject *)self;
}

void MGLVertexArray_tp_dealloc(MGLVertexArray * self) {
	MGLVertexArray_Type.tp_free((PyObject *)self);
}

PyObject * MGLVertexArray_render(MGLVertexArray * self, PyObject * args) {
	int mode;
	int vertices;
	int first;
	int instances;

	int args_ok = PyArg_ParseTuple(
		args,
		"IIII",
		&mode,
		&vertices,
		&first,
		&instances
	);

	if (!args_ok) {
		return 0;
	}

	if (vertices < 0) {
		if (self->num_vertices < 0) {
			MGLError_Set("cannot detect the number of vertices");
			return 0;
		}

		vertices = self->num_vertices;
	}

	const GLMethods & gl = self->context->gl;

	gl.UseProgram(self->program->program_obj);
	gl.BindVertexArray(self->vertex_array_obj);

	// TODO: subroutines

	if (self->index_buffer != (MGLBuffer *)Py_None) {
		const void * ptr = (const void *)((GLintptr)first * 4);
		gl.DrawElementsInstanced(mode, vertices, GL_UNSIGNED_INT, ptr, instances);
	} else {
		gl.DrawArraysInstanced(mode, first, vertices, instances);
	}

	Py_RETURN_NONE;
}

PyObject * MGLVertexArray_render_indirect(MGLVertexArray * self, PyObject * args) {
	int mode;
	MGLBuffer * buffer;
	int first;
	int count;

	int args_ok = PyArg_ParseTuple(
		args,
		"IO!II",
		&mode,
		&MGLBuffer_Type,
		&buffer,
		&first,
		&count
	);

	if (!args_ok) {
		return 0;
	}

	const GLMethods & gl = self->context->gl;

	gl.UseProgram(self->program->program_obj);
	gl.BindVertexArray(self->vertex_array_obj);
	gl.BindBuffer(GL_DRAW_INDIRECT_BUFFER, buffer->buffer_obj);

	// TODO: subroutines

	const void * ptr = (const void *)((GLintptr)first * 20);

	if (self->index_buffer != (MGLBuffer *)Py_None) {
		gl.MultiDrawElementsIndirect(mode, GL_UNSIGNED_INT, ptr, count, 20);
	} else {
		gl.MultiDrawArraysIndirect(mode, ptr, count, 20);
	}

	Py_RETURN_NONE;
}

PyObject * MGLVertexArray_transform(MGLVertexArray * self, PyObject * args) {
	MGLBuffer * output;
	int mode;
	int vertices;
	int first;
	int instances;

	int args_ok = PyArg_ParseTuple(
		args,
		"O!IIII",
		&MGLBuffer_Type,
		&output,
		&mode,
		&vertices,
		&first,
		&instances
	);

	if (!args_ok) {
		return 0;
	}

	if (!self->program->num_varyings) {
		MGLError_Set("the program has no varyings");
		return 0;
	}

	if (vertices < 0) {
		if (self->num_vertices < 0) {
			MGLError_Set("cannot detect the number of vertices");
			return 0;
		}

		vertices = self->num_vertices;
	}

	const GLMethods & gl = self->context->gl;

	gl.UseProgram(self->program->program_obj);
	gl.BindVertexArray(self->vertex_array_obj);

	gl.BindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, 0, output->buffer_obj);

	gl.Enable(GL_RASTERIZER_DISCARD);
	gl.BeginTransformFeedback(mode);

	if (self->subroutines) {

		unsigned * subroutines = self->subroutines;

		if (self->program->num_vertex_shader_subroutines) {
			gl.UniformSubroutinesuiv(
				GL_VERTEX_SHADER,
				self->program->num_vertex_shader_subroutines,
				subroutines
			);
			subroutines += self->program->num_vertex_shader_subroutines;
		}

		if (self->program->num_fragment_shader_subroutines) {
			gl.UniformSubroutinesuiv(
				GL_FRAGMENT_SHADER,
				self->program->num_fragment_shader_subroutines,
				subroutines
			);
			subroutines += self->program->num_fragment_shader_subroutines;
		}

		if (self->program->num_geometry_shader_subroutines) {
			gl.UniformSubroutinesuiv(
				GL_GEOMETRY_SHADER,
				self->program->num_geometry_shader_subroutines,
				subroutines
			);
			subroutines += self->program->num_geometry_shader_subroutines;
		}

		if (self->program->num_tess_evaluation_shader_subroutines) {
			gl.UniformSubroutinesuiv(
				GL_TESS_EVALUATION_SHADER,
				self->program->num_tess_evaluation_shader_subroutines,
				subroutines
			);
			subroutines += self->program->num_tess_evaluation_shader_subroutines;
		}

		if (self->program->num_tess_control_shader_subroutines) {
			gl.UniformSubroutinesuiv(
				GL_TESS_CONTROL_SHADER,
				self->program->num_tess_control_shader_subroutines,
				subroutines
			);
		}
	}

	if (self->index_buffer != (MGLBuffer *)Py_None) {
		const void * ptr = (const void *)((GLintptr)first * 4);
		gl.DrawElementsInstanced(mode, vertices, GL_UNSIGNED_INT, ptr, instances);
	} else {
		gl.DrawArraysInstanced(mode, first, vertices, instances);
	}

	gl.EndTransformFeedback();
	gl.Disable(GL_RASTERIZER_DISCARD);
	gl.Flush();

	Py_RETURN_NONE;
}

PyObject * MGLVertexArray_bind(MGLVertexArray * self, PyObject * args) {
	int location;
	const char * type;
	MGLBuffer * buffer;
	const char * format;
	Py_ssize_t offset;
	int stride;
	int divisor;
	int normalize;

	int args_ok = PyArg_ParseTuple(
		args,
		"IsO!snIIp",
		&location,
		&type,
		&MGLBuffer_Type,
		&buffer,
		&format,
		&offset,
		&stride,
		&divisor,
		&normalize
	);

	if (!args_ok) {
		return 0;
	}

	FormatIterator it = FormatIterator(format);
	FormatInfo format_info = it.info();

	if (type[0] == 'f' && normalize) {
		MGLError_Set("invalid normalize");
		return 0;
	}

	if (!format_info.valid || format_info.divisor || format_info.nodes != 1) {
		MGLError_Set("invalid format");
		return 0;
	}

	FormatNode * node = it.next();

	if (!node->type) {
		MGLError_Set("invalid format");
		return 0;
	}

	char * ptr = (char *)offset;

	const GLMethods & gl = self->context->gl;

	gl.BindVertexArray(self->vertex_array_obj);
	gl.BindBuffer(GL_ARRAY_BUFFER, buffer->buffer_obj);

	switch (type[0]) {
		case 'f':
			gl.VertexAttribPointer(location, node->count, node->type, normalize, stride, ptr);
			break;
		case 'i':
			gl.VertexAttribIPointer(location, node->count, node->type, stride, ptr);
			break;
		case 'd':
			gl.VertexAttribLPointer(location, node->count, node->type, stride, ptr);
			break;
		default:
			MGLError_Set("invalid type");
			return 0;
	}

	gl.VertexAttribDivisor(location, divisor);

	gl.EnableVertexAttribArray(location);

	Py_RETURN_NONE;
}

PyObject * MGLVertexArray_release(MGLVertexArray * self) {
	MGLVertexArray_Invalidate(self);
	Py_RETURN_NONE;
}

PyMethodDef MGLVertexArray_tp_methods[] = {
	{"render", (PyCFunction)MGLVertexArray_render, METH_VARARGS, 0},
	{"render_indirect", (PyCFunction)MGLVertexArray_render_indirect, METH_VARARGS, 0},
	{"transform", (PyCFunction)MGLVertexArray_transform, METH_VARARGS, 0},
	{"bind", (PyCFunction)MGLVertexArray_bind, METH_VARARGS, 0},
	{"release", (PyCFunction)MGLVertexArray_release, METH_NOARGS, 0},
	{0},
};

int MGLVertexArray_set_index_buffer(MGLVertexArray * self, PyObject * value, void * closure) {
	if (Py_TYPE(value) != &MGLBuffer_Type) {
		MGLError_Set("the index_buffer must be a Buffer not %s", Py_TYPE(value)->tp_name);
		return -1;
	}

	Py_INCREF(value);
	Py_DECREF(self->index_buffer);
	self->index_buffer = (MGLBuffer *)value;
	self->num_vertices = (int)(self->index_buffer->size / 4);

	return 0;
}

PyObject * MGLVertexArray_get_vertices(MGLVertexArray * self, void * closure) {
	return PyLong_FromLong(self->num_vertices);
}

int MGLVertexArray_set_vertices(MGLVertexArray * self, PyObject * value, void * closure) {
	int vertices = PyLong_AsUnsignedLong(value);

	if (PyErr_Occurred()) {
		MGLError_Set("invalid value for vertices");
		return -1;
	}

	self->num_vertices = vertices;

	return 0;
}

int MGLVertexArray_set_subroutines(MGLVertexArray * self, PyObject * value, void * closure) {
	if (PyTuple_GET_SIZE(value) != self->num_subroutines) {
		MGLError_Set("the number of subroutines is %d not %d", self->num_subroutines, PyTuple_GET_SIZE(value));
		return -1;
	}

	for (int i = 0; i < self->num_subroutines; ++i) {
		PyObject * obj = PyTuple_GET_ITEM(value, i);
		if (Py_TYPE(obj) == &PyLong_Type) {
			self->subroutines[i] = PyLong_AsUnsignedLong(obj);
		} else {
			PyObject * int_cast = PyNumber_Long(obj);
			if (!int_cast) {
				MGLError_Set("invalid values in subroutines");
				return -1;
			}
			self->subroutines[i] = PyLong_AsUnsignedLong(int_cast);
			Py_DECREF(int_cast);
		}
	}

	if (PyErr_Occurred()) {
		MGLError_Set("invalid values in subroutines");
		return -1;
	}

	return 0;
}

PyGetSetDef MGLVertexArray_tp_getseters[] = {
	{(char *)"index_buffer", 0, (setter)MGLVertexArray_set_index_buffer, 0, 0},
	{(char *)"vertices", (getter)MGLVertexArray_get_vertices, (setter)MGLVertexArray_set_vertices, 0, 0},
	{(char *)"subroutines", 0, (setter)MGLVertexArray_set_subroutines, 0, 0},
	{0},
};

PyTypeObject MGLVertexArray_Type = {
	PyVarObject_HEAD_INIT(0, 0)
	"mgl.VertexArray",                                      // tp_name
	sizeof(MGLVertexArray),                                 // tp_basicsize
	0,                                                      // tp_itemsize
	(destructor)MGLVertexArray_tp_dealloc,                  // tp_dealloc
	0,                                                      // tp_print
	0,                                                      // tp_getattr
	0,                                                      // tp_setattr
	0,                                                      // tp_reserved
	0,                                                      // tp_repr
	0,                                                      // tp_as_number
	0,                                                      // tp_as_sequence
	0,                                                      // tp_as_mapping
	0,                                                      // tp_hash
	0,                                                      // tp_call
	0,                                                      // tp_str
	0,                                                      // tp_getattro
	0,                                                      // tp_setattro
	0,                                                      // tp_as_buffer
	Py_TPFLAGS_DEFAULT,                                     // tp_flags
	0,                                                      // tp_doc
	0,                                                      // tp_traverse
	0,                                                      // tp_clear
	0,                                                      // tp_richcompare
	0,                                                      // tp_weaklistoffset
	0,                                                      // tp_iter
	0,                                                      // tp_iternext
	MGLVertexArray_tp_methods,                              // tp_methods
	0,                                                      // tp_members
	MGLVertexArray_tp_getseters,                            // tp_getset
	0,                                                      // tp_base
	0,                                                      // tp_dict
	0,                                                      // tp_descr_get
	0,                                                      // tp_descr_set
	0,                                                      // tp_dictoffset
	0,                                                      // tp_init
	0,                                                      // tp_alloc
	MGLVertexArray_tp_new,                                  // tp_new
};

void MGLVertexArray_Invalidate(MGLVertexArray * array) {
	if (Py_TYPE(array) == &MGLInvalidObject_Type) {
		return;
	}

	// TODO: decref

	const GLMethods & gl = array->context->gl;
	gl.DeleteVertexArrays(1, (GLuint *)&array->vertex_array_obj);

	Py_TYPE(array) = &MGLInvalidObject_Type;
	Py_DECREF(array);
}

void MGLVertexArray_Complete(MGLVertexArray * vertex_array) {
	vertex_array->num_subroutines = 0;
	vertex_array->num_subroutines += vertex_array->program->num_vertex_shader_subroutines;
	vertex_array->num_subroutines += vertex_array->program->num_fragment_shader_subroutines;
	vertex_array->num_subroutines += vertex_array->program->num_geometry_shader_subroutines;
	vertex_array->num_subroutines += vertex_array->program->num_tess_evaluation_shader_subroutines;
	vertex_array->num_subroutines += vertex_array->program->num_tess_control_shader_subroutines;

	if (vertex_array->num_subroutines) {
		vertex_array->subroutines = new unsigned[vertex_array->num_subroutines];
	} else {
		vertex_array->subroutines = 0;
	}
}

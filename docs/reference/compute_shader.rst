ComputeShader
=============

.. py:module:: moderngl
.. py:currentmodule:: moderngl

.. autoclass:: moderngl.ComputeShader

Create
------

.. automethod:: Context.compute_shader(source) -> ComputeShader
    :noindex:

Methods
-------

.. automethod:: ComputeShader.run(group_x=1, group_y=1, group_z=1)
.. automethod:: ComputeShader.get(key, default) -> Union[Uniform, UniformBlock, Subroutine, Attribute, Varying]

Attributes
----------

.. autoattribute:: ComputeShader.source
.. autoattribute:: ComputeShader.glo

.. toctree::
    :maxdepth: 2

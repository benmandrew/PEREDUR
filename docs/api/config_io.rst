config_io.hpp
=============

Parses a TOML configuration file and returns a :cpp:struct:`Config` with overridden values. Fields absent from the file take their default values from :cpp:struct:`Config`.

Pass the file to the ``peredur`` binary with ``--config <file.toml>``.  All sections and keys are optional:

.. code-block:: toml

   [genetic]
   generations     = 20
   population_size = 500

   [fitness]
   weight_syntactic = 0.1
   weight_semantic  = 0.6

   [runtime]
   black_timeout_ms = 2000
   parallel         = 8

A fully-annotated template with every key and its default is provided in ``example-config.toml`` at the repository root.

.. doxygenfile:: config_io.hpp

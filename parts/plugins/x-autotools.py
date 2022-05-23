# -*- Mode:Python; indent-tabs-mode:nil; tab-width:4 -*-

"""This is a modified version of snapcraft's autootools plugin.

This plugin uses the common plugin keywords as well as those for "sources".
For more information check the 'plugins' topic for the former and the
'sources' topic for the latter.

In addition, this plugin uses the following plugin-specific keywords:

    - configflags:
      (list of strings)
      configure flags to pass to the build such as those shown by running
      './configure --help'
"""

import os

import snapcraft


class AutotoolsPlugin(snapcraft.BasePlugin):

    @classmethod
    def schema(cls):
        schema = super().schema()
        schema['properties']['configflags'] = {
            'type': 'array',
            'minitems': 1,
            'uniqueItems': True,
            'items': {
                'type': 'string',
            },
            'default': [],
        }
        schema['properties']['artifacts'] = {
            'type': 'array',
            'minitems': 1,
            'uniqueItems': True,
            'items': {
                'type': 'string',
            },
            'default': [],
        }

        # Inform Snapcraft of the properties associated with building. If these
        # change in the YAML, Snapcraft will consider the build step dirty.
        schema['build-properties'].extend(['configflags'])

        return schema

    def __init__(self, name, options, project):
        super().__init__(name, options, project)
        self.build_packages.append('make')

    def build(self):
        super().build()
        self.run(['./configure'] + self.options.configflags)
        self.run(['make', '-j{}'.format(self.parallel_build_count)])
        for artifact in self.options.artifacts:
            source_path = os.path.join(self.builddir, artifact)
            destination_path = os.path.join(self.installdir, artifact)
            os.makedirs(os.path.dirname(destination_path), exist_ok=True)
            snapcraft.file_utils.link_or_copy(
                source_path, destination_path)

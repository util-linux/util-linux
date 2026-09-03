from setuptools import setup, Extension

pylibmount = Extension(
    'pylibmount',
    sources = [
        'pylibmount.c',
        'fs.c',
        'tab.c',
        'context.c'
    ],
    libraries = ['mount'],
    include_dirs = ['/usr/include/libmount']
)

setup(
    name='libmount',
    version='2.27',
    description = ('parse /etc/fstab, /etc/mtab and /proc/self/mountinfo files, '
                   'manage the mtab file, evaluate mount options, etc'),
    license = 'LGPLv2.1',
    keywords = 'fstab mtab mount',
    url = 'https://www.kernel.org/pub/linux/utils/util-linux/',
    ext_modules = [pylibmount],
    packages = ['libmount'],

    classifiers = [
        "Development Status :: 6 - Mature",
        "License :: OSI Approved :: GNU Lesser General Public License v2 or later (LGPLv2+)",
        "Topic :: Utilities"
    ]
)

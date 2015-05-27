#!/usr/bin/python

import sys
import os
import shutil

from waflib.Node import Node

from wafmodules.filetasks import (
    find_resource_or_fail)

import os.path, sys
sys.path[0:0] = [os.path.join('dependencies', 'AnyPlatform', 'ohWafHelpers')]

from filetasks import gather_files, build_tree, copy_task, find_dir_or_fail, create_copy_task
from utilfuncs import guess_dest_platform, configure_toolchain, guess_ohnet_location, guess_location, guess_openssl_location

def options(opt):
    opt.load('msvc')
    opt.load('compiler_cxx')
    opt.load('compiler_c')
    opt.add_option('--ohnet-include-dir', action='store', default=None)
    opt.add_option('--ohnet-lib-dir', action='store', default=None)
    opt.add_option('--ohnetmon-include-dir', action='store', default=None)
    opt.add_option('--ohnetmon-lib-dir', action='store', default=None)
    opt.add_option('--ohmediaplayer-include-dir', action='store', default=None)
    opt.add_option('--ohmediaplayer-lib-dir', action='store', default=None)
    opt.add_option('--ohnet', action='store', default=None)
    opt.add_option('--ohnetmon', action='store', default=None)
    opt.add_option('--ohmediaplayer', action='store', default=None)
    opt.add_option('--openssl', action='store', default=None)
    opt.add_option('--debug', action='store_const', dest="debugmode", const="Debug", default="Release")
    opt.add_option('--release', action='store_const', dest="debugmode",  const="Release", default="Release")
    opt.add_option('--dest-platform', action='store', default=None)
    opt.add_option('--cross', action='store', default=None)

def configure(conf):

    def set_env(conf, varname, value):
        conf.msg(
                'Setting %s to' % varname,
                "True" if value is True else
                "False" if value is False else
                value)
        setattr(conf.env, varname, value)
        return value

    conf.msg("debugmode:", conf.options.debugmode)
    if conf.options.dest_platform is None:
        try:
            conf.options.dest_platform = guess_dest_platform()
        except KeyError:
            conf.fatal('Specify --dest-platform')

    configure_toolchain(conf)
    guess_ohnet_location(conf)
    guess_location(conf, 'ohNetmon')
    guess_openssl_location(conf)
    guess_location(conf, 'ohMediaPlayer')

    conf.env.dest_platform = conf.options.dest_platform

    if conf.options.dest_platform.startswith('Windows'):
        conf.env.LIB_OHNET=['ws2_32', 'iphlpapi', 'dbghelp']
    conf.env.STLIB_OHNET=['ohNetCore']
    conf.env.STLIB_OHNETMON = ['ohNetmon']
    conf.env.INCLUDES = [
        '.',
        conf.path.find_node('.').abspath()
        ]

    mono = set_env(conf, 'MONO', [] if conf.options.dest_platform.startswith('Windows') else ["mono", "--debug", "--runtime=v4.0"])

def build(bld):

    bld.program(
        source=[
            'Win32/AudioDriver.cpp',
            'Win32/AudioSessionEvents.cpp',
            'Win32/ConfigRegStore.cpp',
            'Win32/ControlPointProxy.cpp',
            'Win32/ExampleMediaPlayer.cpp',
            'Win32/LitePipeTestApp.cpp',
            'Win32/MediaPlayerIF.cpp',
            'Win32/ProcessorPcmWASAPI.cpp',
            'Win32/RamStore.cpp',
            'Win32/UpdateCheck.cpp'
        ],
        use=['OHNET', 'OHMEDIAPLAYER'],
        target='LitePipeSample'
    )

# Bundles
def bundle(ctx):
    print 'TODO - generate installer?'

# == Command for invoking unit tests ==

def test(tst):
    print 'No tests available'

def test_full(tst):
    tst.test_manifest = 'nightly.test'
    test(tst)

# == Contexts to make 'waf test' work ==

from waflib.Build import BuildContext

class TestContext(BuildContext):
    cmd = 'test'
    fun = 'test'

class TestContext(BuildContext):
    cmd = 'test_full'
    fun = 'test_full'

class BundleContext(BuildContext):
    cmd = 'bundle'
    fun = 'bundle'

# vim: set filetype=python softtabstop=4 expandtab shiftwidth=4 tabstop=4:

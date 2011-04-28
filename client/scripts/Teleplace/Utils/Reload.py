# Project OpenQwaq
#
# Copyright (c) 2005-20011, Teleplace, Inc., All Rights Reserved
#
# Redistributions in source code form must reproduce the above
# copyright and this condition.
#
# The contents of this file are subject to the GNU General Public
# License, Version 2 (the "License"); you may not use this file
# except in compliance with the License. A copy of the License is
# available at http://www.opensource.org/licenses/gpl-2.0.php.

# Reload.py: An alternative reload()

import imp
import sys
import types

"""Alternative to reload().

This works by executing the module in a scratch namespace, and then
patching classes, methods and functions.  This avoids the need to
patch instances.  New objects are copied into the target namespace.
"""

def reload_module(mod):
    """Reload a module in place, updating classes, methods and functions.

    Args:
      mod: a module object

    Returns:
      The (updated) input object itself.
    """
    # Get the module name, e.g. 'foo.bar.whatever'
    modname = mod.__name__
    # Get the module namespace (dict) early; this is part of the type check
    modns = mod.__dict__
    # Parse it into package name and module name, e.g. 'foo.bar' and 'whatever'
    i = modname.rfind(".")
    if i >= 0:
        pkgname, modname = modname[:i], modname[i+1:]
    else:
        pkgname = None
    # Compute the search path
    if pkgname:
        # We're not reloading the package, only the module in it
        pkg = sys.modules[pkgname]
        path = pkg.__path__  # Search inside the package
    else:
        # Search the top-level module path
        pkg  = None
        path = None  # Make find_module() uses the default search path
    # Find the module; may raise ImportError
    (stream, filename, (suffix, mode, kind)) = imp.find_module(modname, path)
    # Catch package directories and fix 'em up to __init__.py
    if kind == imp.PKG_DIRECTORY:
        # it's a package directory; retry
        path = mod.__path__
        (stream, filename, (suffix, mode, kind)) = imp.find_module("__init__", path)
    # Turn it into a code object
    try:
        # Is it Python source code or byte code read from a file?
        # XXX Could handle frozen modules, zip-import modules
        if kind not in (imp.PY_COMPILED, imp.PY_SOURCE):
            # Fall back to built-in reload()
            print "Reload: Failed to recognize kind of file " + str(kind)
            print "Reload: stream is: " + str(stream)
            print "Reload: filename is: " + str(filename)
            return reload(mod)
        if kind == imp.PY_SOURCE:
            source = stream.read()
            code = compile(source, filename, "exec")
        elif kind == imp.PY_COMPILED:
            code = marshal.load(stream)
    finally:
        if stream:
            stream.close()
    # Execute the code im a temporary namespace; if this fails, no changes
    tmpns = {}
    exec(code, tmpns)
    # Now we get to the hard part
    oldnames = set(modns)
    newnames = set(tmpns)
    # Add newly introduced names
    for name in newnames - oldnames:
        modns[name] = tmpns[name]
    # Delete names that are no longer current
    for name in oldnames - newnames - set(["__name__"]):
        del modns[name]
    # Now update the rest in place
    for name in oldnames & newnames:
        modns[name] = _update(modns[name], tmpns[name])
    # Done!
    return mod


def _update(oldobj, newobj):
    """Update oldobj, if possible in place, with newobj.

    If oldobj is immutable, this simply returns newobj.

    Args:
      oldobj: the object to be updated
      newobj: the object used as the source for the update

    Returns:
      either oldobj, updated in place, or newobj.
    """
    if type(oldobj) is not type(newobj):
        # Cop-out: if the type changed, give up
        return newobj
    if hasattr(newobj, "__reload_update__"):
        # Provide a hook for updating
        return newobj.__reload_update__(oldobj)
    if isinstance(newobj, types.ClassType):
        return _update_class(oldobj, newobj)
    if isinstance(newobj, types.FunctionType):
        return _update_function(oldobj, newobj)
    if isinstance(newobj, types.MethodType):
        return _update_method(oldobj, newobj)
    # XXX Support class methods, static methods, other decorators
    # Not something we recognize, just give up
    return newobj


def _update_function(oldfunc, newfunc):
    """Update a function object."""
    oldfunc.__doc__ = newfunc.__doc__
    oldfunc.__dict__.update(newfunc.__dict__)
    oldfunc.func_code = newfunc.func_code
    oldfunc.func_defaults = newfunc.func_defaults
    # XXX What else?
    return oldfunc


def _update_method(oldmeth, newmeth):
    """Update a method object."""
    # XXX What if im_func is not a function?
    _update_function(oldmeth.im_func, newmeth.im_func)
    return oldmeth


def _update_class(oldclass, newclass):
    """Update a class object."""
    # XXX What about __slots__?
    olddict = oldclass.__dict__
    newdict = newclass.__dict__
    oldnames = set(olddict)
    newnames = set(newdict)
    for name in newnames - oldnames:
        setattr(oldclass, name, newdict[name])
    for name in oldnames - newnames:
        delattr(oldclass, name)
    for name in oldnames & newnames - set(["__dict__", "__doc__"]):
        setattr(oldclass, name,  newdict[name])
    return oldclass


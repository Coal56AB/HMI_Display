"""Keep SCons' no-command-string sentinel intact when ESP-IDF clones actions.

espressif32 6.10.0 deep-copies ElfToBin actions. SCons 4.11.1 uses a
class instance for _null without a deepcopy hook, so the clone is no longer
recognized by CommandAction.strfunction and verbose builds fail to print it.
This compatibility hook affects only this build process, not installed files.
"""
import copy
from SCons.Action import _null


def _preserve_sentinel(self, memo):
    return self


# Older SCons uses a type object and already preserves identity.
if type(_null).__name__ == "_Null" and copy.deepcopy(_null) is not _null:
    type(_null).__deepcopy__ = _preserve_sentinel

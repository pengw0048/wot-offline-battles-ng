"""Restore dynamic scene handle retirement before synchronous removal events."""
import argparse
from pathlib import Path

REMOVE_BEFORE = '''void DynamicSceneProvider::removeObject( const DynamicObjectHandle & handle )
{
\tMF_ASSERT( dynamicObjectHandles_.isValid( handle ) );
\tif (!dynamicObjectHandles_.isValid( handle ))
\t{
\t\treturn;
\t}

\tpObjectChangeView_->notifyObjectsRemoved( this,\x20
\t\t&objects_[objectLookup_[handle.index_]], 1 );

\t// Remove the object
\tremoveObject_internal( handle );
}'''

REMOVE_AFTER = '''void DynamicSceneProvider::removeObject( const DynamicObjectHandle & handle )
{
    // The caller's reference can alias an octree/handle-array entry. Snapshot
    // it before either array is mutated, and make repeated retirement harmless.
    const DynamicObjectHandle removedHandle = handle;
    if (!dynamicObjectHandles_.isValid( removedHandle )) return;
    const SceneObject removedObject = objects_[objectLookup_[removedHandle.index_]];

    // Release membership and its generation before notifying observers. They
    // may synchronously remove the same object, add another, or reallocate the
    // object arrays. No writes to the retired slot follow this notification.
    removeObject_internal( removedHandle );
    pObjectChangeView_->notifyObjectsRemoved( this, &removedObject, 1 );
}'''

TAIL_BEFORE = '''\tobjects_.pop_back();
\tobjectHandles_.pop_back();
}'''

TAIL_AFTER = '''    if (objectDirtyFlags_[handle.index_])
    {
        objectDirtyFlags_[handle.index_] = false;
        MF_ASSERT( numDirtyObjects_ > 0 );
        --numDirtyObjects_;
    }
\tobjects_.pop_back();
\tobjectHandles_.pop_back();
    dynamicObjectHandles_.release( handle );
}'''


def replace_once(source, before, after):
    if source.count(after) == 1:
        return source
    if source.count(before) != 1:
        raise ValueError('Unrecognized pinned dynamic-scene ownership seam: ' + repr(before))
    return source.replace(before, after)


def patch(root):
    path = Path(root) / 'lib/space/dynamic_scene_provider.cpp'
    source = path.read_text(encoding='utf-8-sig')
    source = replace_once(source, REMOVE_BEFORE, REMOVE_AFTER)
    source = replace_once(source, TAIL_BEFORE, TAIL_AFTER)
    path.write_text(source, encoding='utf-8')
    print('Dynamic scene removal now retires real handles before observer notification')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('engine_root', type=Path)
    patch(parser.parse_args().engine_root)

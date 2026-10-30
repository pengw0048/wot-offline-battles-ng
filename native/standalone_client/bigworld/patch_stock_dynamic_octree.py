#!/usr/bin/env python3
"""Move only dirty dynamic objects between octree leaves instead of rebuilding both trees."""
import argparse
from pathlib import Path


EDITS = {
    'lib/space/dynamic_scene_provider.cpp': (
        (r'''	}

	template <class SweepStartT, class SweepEndT>
	static bool collideReferences( 
''',
         r'''	}

	static void moveObjectNode( DynamicObjectHandle handle,
		BoundingBoxCollection& bbCollection, Octree& tree, OctreeContents& contents,
		Octree::NodeIndex *index, BW::vector<Octree::NodeIndex>& touched )
	{
		if (*index != Octree::INVALID_NODE)
		{
			touched.push_back( *index );
		}
		removeObjectNode( handle, *index, tree, contents );
		insertObjectNode( handle, bbCollection, tree, contents, index, false );
		if (*index != Octree::INVALID_NODE)
		{
			touched.push_back( *index );
		}
	}

	// A leaf bound is the union of its contents; an emptied leaf keeps a point
	// at its centre because updateHeirarchy requires leaf bounds to be valid.
	static void refitLeaves( BW::vector<Octree::NodeIndex>& leaves,
		BoundingBoxCollection& bbCollection, Octree& tree, OctreeContents& contents )
	{
		std::sort( leaves.begin(), leaves.end() );
		leaves.erase( std::unique( leaves.begin(), leaves.end() ), leaves.end() );
		for (BW::vector<Octree::NodeIndex>::iterator iter = leaves.begin();
			iter != leaves.end(); ++iter)
		{
			const OctreeNodeContents& nodeContents = contents[tree.dataOnLeaf( *iter )];
			AABB bound;
			for (OctreeNodeContents::const_iterator item = nodeContents.begin();
				item != nodeContents.end(); ++item)
			{
				bound.addBounds( bbCollection[item->index_] );
			}
			if (bound.insideOut())
			{
				const Vector3 centre = tree.storage().centerOfNode( *iter );
				bound.setBounds( centre, centre );
			}
			tree.storage().boundOfNode( *iter ) = bound;
		}
	}

	template <class SweepStartT, class SweepEndT>
	static bool collideReferences( 
'''),
        (r'''	}

	// Reset entire trees and reinsert everything
	collisionTree_.reinitialise();
	visibilityTree_.reinitialise();

	Detail::clearTreeContents( collisionTreeContents_ );
	Detail::clearTreeContents( visibilityTreeContents_ );

	// Allocate the scene object buffer for pushing to an update message
	BW::vector<SceneObject> updatedObjects;
	updatedObjects.reserve( numDirtyObjects_ );

	// Handle<24,8> bounds both N and the two insertion attempts per object to int32.
	PROFILER_COUNTER( DynamicScene_ReinsertAttempts, static_cast<int32>(objectHandles_.size() * 2) );
	// Feed all the latest data into the core trees
	for (ObjectHandleCollection::iterator iter = objectHandles_.begin();
		iter != objectHandles_.end(); ++iter)
''',
         r'''	}

	// Allocate the scene object buffer for pushing to an update message
	BW::vector<SceneObject> updatedObjects;
	updatedObjects.reserve( numDirtyObjects_ );

	// About 1% of the dynamic objects move per frame: move only those between
	// leaves, then refit the leaves they left or joined.
	BW::vector<Octree::NodeIndex> collisionLeaves;
	BW::vector<Octree::NodeIndex> visibilityLeaves;
	collisionLeaves.reserve( 2 * numDirtyObjects_ );
	visibilityLeaves.reserve( 2 * numDirtyObjects_ );
	// Handle<24,8> bounds both N and the two insertion attempts per object to int32.
	PROFILER_COUNTER( DynamicScene_ReinsertAttempts, static_cast<int32>(numDirtyObjects_ * 2) );
	for (ObjectHandleCollection::iterator iter = objectHandles_.begin();
		iter != objectHandles_.end(); ++iter)
'''),
        (r'''				latestObjectWorldXform_[ handle.index_ ];

			updatedObjects.push_back( objects_[ objectLookup_[handle.index_]] );
		}
	}

	// Now insert all dynamic objects into these trees again and update their
	// tree info
	for (ObjectHandleCollection::iterator iter = objectHandles_.begin();
		iter != objectHandles_.end(); ++iter)
	{
		const DynamicObjectHandle& handle = *iter;

		DynamicObjectStorageInfo& treeInfo = objectStorageInfo_[handle.index_];
		Detail::insertObjectNode( handle, objectCollisionBBs_, collisionTree_, 
			collisionTreeContents_, &treeInfo.collisionNode_, false);
	}

	for (ObjectHandleCollection::iterator iter = objectHandles_.begin();
		iter != objectHandles_.end(); ++iter)
	{
		const DynamicObjectHandle& handle = *iter;

		DynamicObjectStorageInfo& treeInfo = objectStorageInfo_[handle.index_];
		Detail::insertObjectNode( handle, objectVisibilityBBs_, visibilityTree_, 
			visibilityTreeContents_, &treeInfo.visibilityNode_, false);
	}

	collisionTree_.updateHeirarchy();
''',
         r'''				latestObjectWorldXform_[ handle.index_ ];

			DynamicObjectStorageInfo& treeInfo = objectStorageInfo_[handle.index_];
			Detail::moveObjectNode( handle, objectCollisionBBs_, collisionTree_,
				collisionTreeContents_, &treeInfo.collisionNode_, collisionLeaves );
			Detail::moveObjectNode( handle, objectVisibilityBBs_, visibilityTree_,
				visibilityTreeContents_, &treeInfo.visibilityNode_, visibilityLeaves );

			updatedObjects.push_back( objects_[ objectLookup_[handle.index_]] );
		}
	}

	Detail::refitLeaves( collisionLeaves, objectCollisionBBs_, collisionTree_,
		collisionTreeContents_ );
	Detail::refitLeaves( visibilityLeaves, objectVisibilityBBs_, visibilityTree_,
		visibilityTreeContents_ );

	collisionTree_.updateHeirarchy();
'''),
    ),
}


def replace_once(text, old, new, label):
    if new in text:
        return text
    if text.count(old) != 1:
        raise RuntimeError("Unexpected public engine source for " + label)
    return text.replace(old, new, 1)


def patch(root):
    for relative, edits in EDITS.items():
        path = Path(root) / relative
        text = path.read_text()
        for old, new in edits:
            text = replace_once(text, old, new, relative)
        path.write_text(text)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("engine_root", type=Path)
    patch(parser.parse_args().engine_root)


if __name__ == "__main__":
    main()

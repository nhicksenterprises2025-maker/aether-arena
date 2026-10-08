"""Verify collection reuse without requiring Blender or generating art files."""
import ast
import os
from pathlib import Path
from types import SimpleNamespace
import unittest

GAME_ROOT = Path(os.environ.get(
    'RIFT_GAME_ROOT',
    str(Path(__file__).resolve().parents[1]),
))
source = GAME_ROOT.joinpath('blender/generate_models.py').read_text(encoding='utf-8')
tree = ast.parse(source)
definition = next(node for node in tree.body if isinstance(node, ast.FunctionDef) and node.name == 'new_collection')


class Collections(dict):
    def new(self, name):
        self[name] = SimpleNamespace(name=name)
        return self[name]


class Children(dict):
    def link(self, collection):
        if collection.name in self:
            raise AssertionError('Collection linked twice')
        self[collection.name] = collection


class CollectionRegression(unittest.TestCase):
    def setUp(self):
        self.collections = Collections()
        self.children = Children()
        bpy = SimpleNamespace(
            data=SimpleNamespace(collections=self.collections),
            context=SimpleNamespace(scene=SimpleNamespace(collection=SimpleNamespace(children=self.children))),
        )
        scope = {'bpy': bpy}
        exec(compile(ast.Module(body=[definition], type_ignores=[]), '<new_collection>', 'exec'), scope)
        self.new_collection = scope['new_collection']

    def test_new_collection_gets_canonical_name(self):
        collection = self.new_collection('ironclad')
        self.assertEqual(collection.name, 'ironclad')
        self.assertIs(self.children['ironclad'], collection)

    def test_rerun_reuses_existing_collection_without_double_link(self):
        first = self.new_collection('ironclad')
        second = self.new_collection('ironclad')
        self.assertIs(second, first)
        self.assertEqual(list(self.collections), ['ironclad'])

    def test_existing_unlinked_collection_is_linked(self):
        first = self.collections.new('tower_core')
        self.assertIs(self.new_collection('tower_core'), first)
        self.assertIs(self.children['tower_core'], first)


if __name__ == '__main__':
    unittest.main()

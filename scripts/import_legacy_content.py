#!/usr/bin/env python3
"""Import the reviewed, source-only HUNR SQL into deterministic C++ content.

This reads SQL text, never executes SQL, and never reads the original live dump.
No dependencies beyond Python's standard library. Unknown SQL is rejected.
"""
from __future__ import annotations

import argparse
from collections import Counter
from dataclasses import dataclass
from decimal import Decimal
import hashlib
import json
from pathlib import Path
import re
import sys
import unittest


STATIC_TABLES = frozenset("""
array_head_2_frames nr_achievement nr_arrow nr_background_item
nr_bo_mong_boss_config nr_bo_mong_config nr_bo_mong_moc_diem
nr_bo_mong_nhiem_vu_config nr_caption nr_collection_book nr_dart nr_drop_rate
nr_effect nr_effect_data nr_image nr_image_by_name nr_item nr_item_option_template
nr_lucky_wheel nr_map nr_mob_template nr_npc_template nr_others nr_part nr_power
nr_salon nr_shop_amulet nr_shop_bulma nr_shop_bunma_tet nr_shop_equipment
nr_shop_food_kemdau nr_shop_food_mily nr_shop_food_pudding nr_shop_food_sushi
nr_shop_food_xucxich nr_shop_huy_diet nr_shop_itemdetu nr_shop_linhthu nr_shop_ltn
nr_shop_lytieunuong nr_shop_popo nr_shop_santa nr_shop_thientu nr_shop_tv nr_shop_uron
nr_shop_whis nr_skill nr_skill_disciple nr_skill_option_template nr_skill_paint
nr_special_skill nr_task
""".split())
OTHER_KEYS = frozenset(("avatar", "shop", "tile_set", "open_power"))
NUMBER = re.compile(r"[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?")
WORD = re.compile(r"[A-Za-z_][A-Za-z_0-9$]*")


@dataclass(frozen=True)
class Token:
    kind: str
    value: str
    begin: int
    end: int


def lex(source: str):
    pos = 0
    while pos < len(source):
        ch = source[pos]
        if ch.isspace():
            pos += 1
            continue
        if source.startswith("--", pos) and (pos + 2 == len(source) or source[pos + 2].isspace()):
            newline = source.find("\n", pos)
            pos = len(source) if newline < 0 else newline + 1
            continue
        if ch == "#":
            newline = source.find("\n", pos)
            pos = len(source) if newline < 0 else newline + 1
            continue
        if source.startswith("/*", pos):
            end = source.find("*/", pos + 2)
            if end < 0 or source.startswith("/*!", pos):
                raise ValueError("Unterminated or executable SQL comment")
            pos = end + 2
            continue
        begin = pos
        if ch in "'`\"":
            quote = ch
            pos += 1
            value = []
            while pos < len(source):
                ch = source[pos]
                pos += 1
                if ch == quote:
                    if pos < len(source) and source[pos] == quote:
                        value.append(ch)
                        pos += 1
                    else:
                        break
                elif ch == "\\" and quote != "`":
                    if pos == len(source):
                        raise ValueError("Unterminated SQL escape")
                    escaped = source[pos]
                    pos += 1
                    # MySQL preserves the backslash for LIKE escapes, and removes
                    # it for unrecognised string escapes (NO_BACKSLASH_ESCAPES off).
                    value.append({"0": "\0", "b": "\b", "n": "\n", "r": "\r",
                                  "t": "\t", "Z": "\x1a", "%": "\\%", "_": "\\_"}.get(escaped, escaped))
                else:
                    value.append(ch)
            else:
                raise ValueError("Unterminated SQL quoted literal")
            yield Token("identifier" if quote == "`" else "string", "".join(value), begin, pos)
            continue
        match = NUMBER.match(source, pos)
        if match:
            pos = match.end()
            yield Token("number", match.group(), begin, pos)
            continue
        match = WORD.match(source, pos)
        if match:
            pos = match.end()
            yield Token("word", match.group(), begin, pos)
            continue
        if ch in "(),;=":
            pos += 1
            yield Token(ch, ch, begin, pos)
            continue
        raise ValueError(f"Unsupported SQL character {ch!r} at offset {pos}")


def statements(source: str):
    pending = []
    for token in lex(source):
        if token.kind == ";":
            if pending:
                yield pending
                pending = []
        else:
            pending.append(token)
    if pending:
        raise ValueError("SQL statement is missing its terminating semicolon")


def matches(tokens, *words):
    return len(tokens) >= len(words) and all(t.value.upper() == w for t, w in zip(tokens, words))


def parse_schema(source: str):
    result = {}
    for tokens in statements(source):
        if matches(tokens, "SET", "NAMES", "UTF8MB4") and len(tokens) == 3:
            continue
        if not matches(tokens, "CREATE", "TABLE") or tokens[2].kind != "identifier" or tokens[3].kind != "(":
            raise ValueError("Schema accepts only CREATE TABLE and SET NAMES utf8mb4")
        table = tokens[2].value
        if table in result:
            raise ValueError(f"Duplicate schema table {table}")
        depth, groups, group = 1, [], []
        for token in tokens[4:]:
            if token.kind == "(":
                depth += 1
            elif token.kind == ")":
                depth -= 1
                if depth == 0:
                    groups.append(group)
                    break
            if token.kind == "," and depth == 1:
                groups.append(group)
                group = []
            else:
                group.append(token)
        if depth != 0:
            raise ValueError(f"Unterminated schema table {table}")
        columns, primary_key = [], []
        for group in groups:
            if group[0].kind == "identifier":
                definition = source[group[1].begin:group[-1].end]
                columns.append({"name": group[0].value, "sql_type": group[1].value.lower(),
                                "nullable": not any(matches(group[i:], "NOT", "NULL") for i in range(len(group))),
                                "definition": definition})
            elif matches(group, "PRIMARY", "KEY"):
                primary_key = [token.value for token in group[2:] if token.kind == "identifier"]
            elif not (matches(group, "INDEX") or matches(group, "UNIQUE", "INDEX") or matches(group, "CONSTRAINT")):
                raise ValueError(f"Unsupported schema constraint in {table}")
        if not columns or len({c['name'] for c in columns}) != len(columns):
            raise ValueError(f"Missing or duplicate schema columns in {table}")
        result[table] = {"columns": columns, "primary_key": primary_key}
    return result


def exact_json(value):
    """Avoid a binary-float conversion and nlohmann's >uint64 fallback to double."""
    if isinstance(value, Decimal):
        return {"$sql_decimal": str(value)}
    if isinstance(value, int) and not isinstance(value, bool) and not -(2**63) <= value <= 2**64 - 1:
        return {"$sql_integer": str(value)}
    if isinstance(value, list):
        return [exact_json(v) for v in value]
    if isinstance(value, dict):
        return {k: exact_json(v) for k, v in value.items()}
    return value


def unique_object(pairs):
    obj = {}
    for key, value in pairs:
        if key in obj:
            raise ValueError(f"Duplicate embedded JSON key {key}")
        obj[key] = value
    return obj


def parse_embedded(value: str):
    return exact_json(json.loads(value, parse_float=Decimal, object_pairs_hook=unique_object,
                                 parse_constant=lambda value: (_ for _ in ()).throw(ValueError(f"Invalid JSON {value}"))))


def normalize_java_json(value: str):
    """org.json accepts trailing commas; remove only commas outside strings."""
    output, quoted, escaped = [], False, False
    for pos, ch in enumerate(value):
        if quoted:
            output.append(ch)
            if escaped:
                escaped = False
            elif ch == '\\':
                escaped = True
            elif ch == '"':
                quoted = False
        elif ch == '"':
            quoted = True
            output.append(ch)
        elif ch == ',':
            next_pos = pos + 1
            while next_pos < len(value) and value[next_pos].isspace():
                next_pos += 1
            if next_pos == len(value) or value[next_pos] not in ']}':
                output.append(ch)
        else:
            output.append(ch)
    return ''.join(output)


def sql_value(token):
    if token.kind == "string":
        return token.value
    if token.kind == "number":
        return exact_json(Decimal(token.value) if any(c in token.value.lower() for c in ".e") else int(token.value))
    if token.kind == "word" and token.value.upper() == "NULL":
        return None
    raise ValueError(f"Unsupported SQL value {token.value!r}; expressions are never executed")


def parse_content(source: str, schemas):
    tables = {}
    for tokens in statements(source):
        if (matches(tokens, "SET", "NAMES", "UTF8MB4") and len(tokens) == 3 or
                matches(tokens, "START", "TRANSACTION") and len(tokens) == 2 or
                matches(tokens, "COMMIT") and len(tokens) == 1):
            continue
        if not matches(tokens, "INSERT", "INTO") or len(tokens) < 5 or tokens[2].kind != "identifier":
            raise ValueError("Content accepts only literal INSERT INTO VALUES statements")
        table = tokens[2].value
        if table not in STATIC_TABLES:
            raise ValueError(f"Non-static/private table is not importable: {table}")
        if table not in schemas:
            raise ValueError(f"No schema for {table}")
        columns = [c['name'] for c in schemas[table]['columns']]
        pos = 3
        if tokens[pos].kind == "(":
            pos += 1
            explicit = []
            while pos < len(tokens) and tokens[pos].kind != ")":
                if tokens[pos].kind != "identifier":
                    raise ValueError(f"Expected INSERT column in {table}")
                explicit.append(tokens[pos].value)
                pos += 1
                if tokens[pos].kind == ",":
                    pos += 1
                    if pos >= len(tokens) or tokens[pos].kind != "identifier":
                        raise ValueError("INSERT column list has a trailing comma")
                elif tokens[pos].kind != ")":
                    raise ValueError("Invalid INSERT columns")
            pos += 1
            if len(set(explicit)) != len(explicit) or set(explicit) != set(columns):
                raise ValueError(f"INSERT must specify all schema columns once: {table}")
            columns = explicit
        if pos >= len(tokens) or tokens[pos].value.upper() != "VALUES":
            raise ValueError("Only INSERT VALUES is supported")
        pos += 1
        target = tables.setdefault(table, {"rows": [], "embedded_json": [], "json_normalizations": []})
        while pos < len(tokens):
            if tokens[pos].kind != "(":
                raise ValueError("Expected a VALUES tuple")
            pos += 1
            values = []
            for index in range(len(columns)):
                if pos >= len(tokens):
                    raise ValueError(f"Truncated row in {table}")
                values.append(sql_value(tokens[pos]))
                pos += 1
                delimiter = ")" if index + 1 == len(columns) else ","
                if pos >= len(tokens) or tokens[pos].kind != delimiter:
                    raise ValueError(f"Column count or delimiter mismatch in {table}")
                pos += 1
            row = dict(zip(columns, values))
            if table == "nr_others" and row['key'] not in OTHER_KEYS:
                raise ValueError("Private/unreviewed nr_others key")
            for column in schemas[table]['columns']:
                if row[column['name']] is None and not column['nullable']:
                    raise ValueError(f"NULL in required column {table}.{column['name']}")
            embedded = {}
            for key, value in row.items():
                # Some prose starts with '['; only valid JSON containers are
                # decoded. Typed fields below must decode and pass validation.
                if isinstance(value, str) and value.lstrip().startswith(("[", "{")):
                    try:
                        embedded[key] = parse_embedded(value)
                    except (ValueError, json.JSONDecodeError):
                        normalized = normalize_java_json(value)
                        try:
                            embedded[key] = parse_embedded(normalized)
                            target['json_normalizations'].append({'row_index': len(target['rows']), 'column': key,
                                                                  'kind': 'org.json trailing comma'})
                        except (ValueError, json.JSONDecodeError):
                            pass
            target['rows'].append(row)
            target['embedded_json'].append(embedded)
            if pos < len(tokens):
                if tokens[pos].kind != "," or pos + 1 == len(tokens):
                    raise ValueError("Unexpected content after VALUES tuple")
                pos += 1
    for table, data in tables.items():
        keys = schemas[table]['primary_key']
        seen = set()
        for row in data['rows']:
            key = tuple(json.dumps(row[column], sort_keys=True) for column in keys)
            if keys and key in seen:
                raise ValueError(f"Duplicate primary key in {table}: {key}")
            seen.add(key)
    return tables


def integer(value, low, high, label):
    if isinstance(value, bool) or not isinstance(value, int) or not low <= value <= high:
        raise ValueError(f"{label} must be an integer in {low}..{high}")
    return value


def validate_skills(tables):
    keys, level_ids, classes = set(), set(), Counter()
    for row, embedded in zip(tables['nr_skill']['rows'], tables['nr_skill']['embedded_json']):
        key = (integer(row['class'], 0, 2**31-1, "skill class"), integer(row['skill_id'], 0, 127, "skill_id"))
        if key in keys:
            raise ValueError(f"Duplicate class/skill key {key}")
        keys.add(key)
        classes[key[0]] += 1
        levels = embedded.get('skills')
        if not isinstance(levels, list):
            raise ValueError(f"Invalid embedded skills array for {key}")
        max_point = integer(row['max_point'], 1, 127, "max_point")
        points = set()
        for level in levels:
            # DANH/CHUONG in class 4 have their actual Java point 0.
            point = integer(level['point'], 0, max_point, "point")
            level_id = integer(level['id'], 0, 32767, "level id")
            if point in points:
                raise ValueError(f"Duplicate skill point {key}/{point}")
            points.add(point)
            # Repeated level IDs across class templates are intentional (shield).
            level_ids.add(level_id)
            integer(level['power_require'], 0, 2**63-1, "power_require")
            for field in ('cool_down', 'max_fight', 'mana_use', 'dx', 'dy'):
                integer(level[field], 0, 2**31-1, field)
            for field in ('damage', 'price'):
                integer(level[field], -(2**31), 2**31-1, field)
            if not isinstance(level['more_info'], str):
                raise ValueError("more_info must be a string")
    return {"skill_templates": len(keys), "skill_levels": sum(len(e['skills']) for e in tables['nr_skill']['embedded_json']),
            "distinct_skill_level_ids": len(level_ids), "skills_per_class": dict(sorted(classes.items()))}


def java_int(value, label):
    if isinstance(value, str) and re.fullmatch(r'[+-]?[0-9]+', value):
        value = int(value)
    return integer(value, -(2**31), 2**31 - 1, label)


def validate_references(tables):
    ids = {name: {row['id'] for row in data['rows']} for name, data in tables.items()}
    item_options, map_mobs, map_npcs, waypoints = 0, 0, 0, 0
    for row, embedded in zip(tables['nr_item']['rows'], tables['nr_item']['embedded_json']):
        if row['options'] is not None and not isinstance(embedded.get('options'), list):
            raise ValueError(f"Invalid item options JSON for item {row['id']}")
        for option in embedded.get('options', []):
            option_id = java_int(option['id'], 'item option id')
            java_int(option['param'], 'item option param')
            if option_id not in ids['nr_item_option_template']:
                raise ValueError(f"Unknown option {option_id} on item {row['id']}")
            item_options += 1
    for row, embedded in zip(tables['nr_map']['rows'], tables['nr_map']['embedded_json']):
        for field in ('waypoint', 'mob', 'npc', 'position_bg_item', 'effect', 'effect_event'):
            if row[field] is not None and not isinstance(embedded.get(field), list):
                raise ValueError(f"Invalid {field} JSON on map {row['id']}")
        for mob in embedded['mob']:
            if java_int(mob['id'], 'map mob id') not in ids['nr_mob_template']:
                raise ValueError(f"Unknown mob on map {row['id']}")
            map_mobs += 1
        for npc in embedded['npc']:
            if java_int(npc['id'], 'map npc id') not in ids['nr_npc_template']:
                raise ValueError(f"Unknown NPC on map {row['id']}")
            map_npcs += 1
        for waypoint in embedded['waypoint']:
            if java_int(waypoint['next'], 'waypoint next') not in ids['nr_map']:
                raise ValueError(f"Unknown waypoint destination on map {row['id']}")
            waypoints += 1
    return {'validated_item_option_references': item_options, 'validated_map_mob_references': map_mobs,
            'validated_map_npc_references': map_npcs, 'validated_waypoint_references': waypoints}


def make_snapshot(schema_path: Path, content_path: Path):
    schema_bytes, content_bytes = schema_path.read_bytes(), content_path.read_bytes()
    schemas = parse_schema(schema_bytes.decode('utf-8-sig'))
    tables = parse_content(content_bytes.decode('utf-8-sig'), schemas)
    counts = {key: len(value['rows']) for key, value in sorted(tables.items())}
    return {"format": "hunr-source-content", "version": 1,
            "inputs": {"schema.sql": {"sha256": hashlib.sha256(schema_bytes).hexdigest()},
                       "content.sql": {"sha256": hashlib.sha256(content_bytes).hexdigest()}},
            "counts": {"schema_tables": len(schemas), "content_tables": len(tables),
                       "rows": sum(counts.values()), "rows_per_table": counts, **validate_skills(tables),
                       **validate_references(tables)},
            "schema": schemas, "tables": tables}


def verify_source_manifest(snapshot, manifest_path: Path):
    manifest_bytes = manifest_path.read_bytes()
    manifest = json.loads(manifest_bytes.decode('utf-8-sig'))
    for filename, field in (('schema.sql', 'schemaSha256'), ('content.sql', 'contentSha256')):
        if snapshot['inputs'][filename]['sha256'].lower() != manifest[field].lower():
            raise ValueError(f"{filename} SHA-256 differs from reviewed source manifest; review/reprepare the SQL first")
    if snapshot['counts']['schema_tables'] != manifest['schemaTables']:
        raise ValueError("Schema table count differs from reviewed source manifest")
    if snapshot['counts']['rows_per_table'] != manifest['contentInsertCounts']:
        raise ValueError("Static row counts differ from reviewed source manifest")
    snapshot['inputs']['source-content-manifest.json'] = {'sha256': hashlib.sha256(manifest_bytes).hexdigest()}


def serialize(snapshot):
    # One record per line keeps source changes reviewable, including large paint arrays.
    compact = lambda value: json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(',', ':'))
    lines = ['{']
    for name, value in sorted(snapshot.items()):
        if name == 'tables':
            continue
        lines.append(f'  {json.dumps(name)}: {compact(value)},')
    lines.append('  "tables": {')
    for table_index, (name, table) in enumerate(sorted(snapshot['tables'].items())):
        lines.append(f'    {json.dumps(name)}: {{')
        lines.append(f'      "json_normalizations": {compact(table["json_normalizations"])},')
        for field_index, field in enumerate(('rows', 'embedded_json')):
            lines.append(f'      {json.dumps(field)}: [')
            for index, row in enumerate(table[field]):
                lines.append('        ' + compact(row) + (',' if index + 1 < len(table[field]) else ''))
            lines.append('      ]' + (',' if field_index == 0 else ''))
        lines.append('    }' + (',' if table_index + 1 < len(snapshot['tables']) else ''))
    lines.extend(['  }', '}', ''])
    return '\n'.join(lines)


class ParserTests(unittest.TestCase):
    def test_quoted_semicolons_and_mysql_escapes(self):
        tokens = list(statements("INSERT INTO `x` VALUES ('a;it''s', 'a\\n\\\\b\\\"c', NULL); -- end\n"))
        self.assertEqual(tokens[0][5].value, "a;it's")
        self.assertEqual(tokens[0][7].value, 'a\n\\b"c')
        self.assertEqual(sql_value(tokens[0][9]), None)

    def test_precise_numbers(self):
        self.assertEqual(parse_embedded('[9007199254740993,0.123456789012345678901,18446744073709551616]'),
                         [9007199254740993, {"$sql_decimal": "0.123456789012345678901"}, {"$sql_integer": "18446744073709551616"}])

    def test_embedded_object_duplicates_and_nan_rejected(self):
        for value in ('{"id":1,"id":2}', '[NaN]', '[Infinity]'):
            with self.assertRaises(ValueError):
                parse_embedded(value)

    def test_java_trailing_comma_preserves_string_contents(self):
        source = '[{"text":"keep ,] and \\\"quote\\\"","id":1,},]'
        self.assertEqual(parse_embedded(normalize_java_json(source)), [{'text': 'keep ,] and "quote"', 'id': 1}])

    def test_multiline_schema_and_multiple_values(self):
        schema = parse_schema("CREATE TABLE `nr_skill_option_template` (`id` int NOT NULL, `name` varchar(100) NOT NULL COMMENT 'a,b)', PRIMARY KEY (`id`)) ENGINE=InnoDB;")
        table = parse_content("INSERT INTO `nr_skill_option_template` VALUES (1,'a'),(2,'b');", schema)
        self.assertEqual(table['nr_skill_option_template']['rows'][1], {'id': 2, 'name': 'b'})
        with self.assertRaises(ValueError):
            parse_content("INSERT INTO `nr_skill_option_template` VALUES (1,'a'),(1,'b');", schema)

    def test_explicit_column_order_and_invalid_trailing_comma(self):
        schema = parse_schema("CREATE TABLE `nr_skill_option_template` (`id` int NOT NULL, `name` text NOT NULL, PRIMARY KEY (`id`));")
        table = parse_content("INSERT INTO `nr_skill_option_template` (`name`,`id`) VALUES ('x',4);", schema)
        self.assertEqual(table['nr_skill_option_template']['rows'][0], {'id': 4, 'name': 'x'})
        with self.assertRaises(ValueError):
            parse_content("INSERT INTO `nr_skill_option_template` (`id`,`name`,) VALUES (4,'x');", schema)

    def test_embedded_json_retains_original_sql_string(self):
        schema = parse_schema("CREATE TABLE `nr_arrow` (`id` int NOT NULL, `img` text NOT NULL, PRIMARY KEY (`id`));")
        table = parse_content("INSERT INTO `nr_arrow` VALUES (1,'[ 1, 2, 3 ]');", schema)['nr_arrow']
        self.assertEqual(table['rows'][0]['img'], '[ 1, 2, 3 ]')
        self.assertEqual(table['embedded_json'][0]['img'], [1, 2, 3])

    def test_private_other_key_rejected(self):
        schema = parse_schema("CREATE TABLE `nr_others` (`id` int NOT NULL, `key` text NOT NULL, `value` text, PRIMARY KEY (`id`));")
        with self.assertRaises(ValueError):
            parse_content("INSERT INTO `nr_others` VALUES (1,'private_server_password','x');", schema)

    def test_rejects_partial_and_executable_input(self):
        for source in ("INSERT INTO `nr_user` VALUES (1);", "DELETE FROM `nr_skill`;", "/*!40101 SET x=1 */;", "COMMIT"):
            with self.assertRaises(ValueError):
                parse_content(source, {})


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sql-dir', type=Path, default=root / 'Sample game old/HUNR_Server_Java/Hunr2026/sql')
    parser.add_argument('--output', type=Path, default=root / 'content/hunr/content.json')
    parser.add_argument('--check', action='store_true', help='Verify that committed content matches inputs without writing')
    parser.add_argument('--self-test', action='store_true', help='Run only Python parser fixtures')
    args = parser.parse_args()
    if args.self_test:
        result = unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(ParserTests))
        return 0 if result.wasSuccessful() else 1
    snapshot = make_snapshot(args.sql_dir / 'schema.sql', args.sql_dir / 'content.sql')
    verify_source_manifest(snapshot, args.sql_dir / 'source-content-manifest.json')
    serialized = serialize(snapshot)
    if args.check:
        if not args.output.exists() or args.output.read_text(encoding='utf-8') != serialized:
            raise ValueError("Generated content is missing or stale; rerun the importer without --check")
    else:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(serialized, encoding='utf-8', newline='\n')
    print(json.dumps({'output': str(args.output), 'checked': args.check, 'inputs': snapshot['inputs'], 'counts': snapshot['counts']}, ensure_ascii=False, indent=2))
    return 0


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except (ValueError, KeyError, IndexError, OSError) as error:
        print(f"Import failed: {error}", file=sys.stderr)
        raise SystemExit(1)

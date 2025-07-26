// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

/* eslint-disable prefer-const */

import { lstat } from 'fs/promises';
import { delimiter, extname, resolve } from 'path';
import { isScalar } from 'yaml';
import { postscriptVariable, undoVariableName } from '../constants';
import { i } from '../i18n';
import { Exports } from '../interfaces/metadata/exports';
import { Session } from '../session';
import { Channels } from '../util/channels';
import { isIterable } from '../util/checks';
import { replaceCurlyBraces } from '../util/curly-replacements';
import { linq } from '../util/linq';
import { Queue } from '../util/promise';
import { Uri } from '../util/uri';
// eslint-disable-next-line @typescript-eslint/no-require-imports
const XMLWriterImpl = require('xml-writer');

export interface XmlWriter {
  startDocument(version: string | undefined, encoding: string | undefined): XmlWriter;
  writeElement(name: string, content: string): XmlWriter;
  writeAttribute(name: string, value: string): XmlWriter;
  startElement(name: string): XmlWriter;
  endElement(): XmlWriter;
}

export interface UndoFile {
  environment: Record<string, string | undefined> | undefined;
  aliases: Array<string> | undefined;
  stack: Array<string> | undefined;
}

function findCaseInsensitiveOnWindows<V>(map: Map<string, V>, key: string): V | undefined {
  return process.platform === 'win32' ? linq.find(map, key) : map.get(key);
}
export type Tuple<K, V> = [K, V];

function displayNoPostScriptError(channels: Channels) {
  channels.error(i`no postscript file: run vcpkg-shell with the same arguments`);
}

export class Activation {
  #defines = new Map<string, string>();
  #aliases = new Map<string, string>();
  #environmentChanges = new Map<string, Set<string>>();
  #properties = new Map<string, Set<string>>();
  #msbuild_properties = new Array<Tuple<string, string>>();

  // Relative to the artifact install
  #locations = new Map<string, string>();
  #paths = new Map<string, Set<string>>();
  #tools = new Map<string, string>();

  private constructor(
    private readonly allowStacking: boolean,
    private readonly channels: Channels,
    private readonly environment: NodeJS.ProcessEnv,
    private readonly postscriptFile: Uri | undefined,
    private readonly undoFile: UndoFile | undefined,
    private readonly nextUndoEnvironmentFile: Uri) {
  }

  static async start(session: Session, allowStacking: boolean) : Promise<Activation> {
    const environment = process.env;
    const postscriptFileName = environment[postscriptVariable];
    const postscriptFile = postscriptFileName ? session.fileSystem.file(postscriptFileName) : undefined;

    const undoVariableValue = environment[undoVariableName];
    const undoFileUri = undoVariableValue ? session.fileSystem.file(undoVariableValue) : undefined;
    const undoFileRaw = undoFileUri ? await undoFileUri.tryReadUTF8() : undefined;
    const undoFile = undoFileRaw ? <UndoFile>JSON.parse(undoFileRaw) : undefined;

    const undoStack = undoFile?.stack;
    if (undoFile && !allowStacking) {
      if (undoStack) {
        printDeactivatingMessage(session.channels, undoStack);
        undoStack.length = 0;
      }

      if (undoFile.environment) {
        // form what the environment "would have been" had we deactivated first for figuring out
        // what the new environment should be
        undoActivation(environment, undoFile.environment);
      }
    }

    const nextUndoEnvironmentFile = session.nextPreviousEnvironment;

    return new Activation(allowStacking, session.channels, environment, postscriptFile, undoFile, nextUndoEnvironmentFile);
  }

  addExports(exports: Exports, targetFolder: Uri) {
    for (let [define, defineValue] of exports.defines) {
      if (!define) {
        continue;
      }

      if (defineValue === 'true') {
        defineValue = '1';
      }
      this.addDefine(define, defineValue);
    }

    // **** paths ****
    for (const [pathName, values] of exports.paths) {
      if (!pathName || !values || values.length === 0) {
        continue;
      }

      // the folder is relative to the artifact install
      for (const folder of values) {
        this.addPath(pathName, targetFolder.join(folder).fsPath);
      }
    }

    // **** tools ****
    for (let [toolName, toolPath] of exports.tools) {
      if (!toolName || !toolPath) {
        continue;
      }
      this.addTool(toolName, targetFolder.join(toolPath).fsPath);
    }

    // **** locations ****
    for (const [name, location] of exports.locations) {
      if (!name || !location) {
        continue;
      }

      this.addLocation(name, targetFolder.join(location).fsPath);
    }

    // **** variables ****
    for (const [name, environmentVariableValues] of exports.environment) {
      if (!name || environmentVariableValues.length === 0) {
        continue;
      }
      this.addEnvironmentVariable(name, environmentVariableValues);
    }

    // **** properties ****
    for (const [name, propertyValues] of exports.properties) {
      if (!name || propertyValues.length === 0) {
        continue;
      }
      this.addProperty(name, propertyValues);
    }

    // **** aliases ****
    for (const [name, alias] of exports.aliases) {
      if (!name || !alias) {
        continue;
      }
      this.addAlias(name, alias);
    }

    // **** msbuild-properties ****
    for (const [name, propertyValue] of exports.msbuild_properties) {
      this.addMSBuildProperty(name, propertyValue, targetFolder);
    }
  }


  /** a collection of #define declarations that would assumably be applied to all compiler calls. */
  addDefine(name: string, value: string) {
    const v = findCaseInsensitiveOnWindows(this.#defines, name);

    if (v === undefined) {
      this.#defines.set(name, value);
    } else if (v !== value) {
      // conflict. todo: what do we want to do?
      this.channels.warning(i`Duplicate define ${name} during activation. New value will replace old.`);
      this.#defines.set(name, value);
    }
  }

  get defines() {
    return linq.entries(this.#defines).selectAsync(async ([key, value]) => <Tuple<string, string>>[key, await this.resolveAndVerify(value)]);
  }

  async getDefine(name: string): Promise<string | undefined> {
    const v = this.#defines.get(name);
    return v ? await this.resolveAndVerify(v) : undefined;
  }

  /** a collection of tool locations from artifacts */
  addTool(name: string, value: string) {
    const t = findCaseInsensitiveOnWindows(this.#tools, name);
    if (t === undefined) {
      this.#tools.set(name, value);
    } else if (t !== value) {
      this.channels.warning(i`Duplicate tool declared ${name} during activation.  New value will replace old.`);
      this.#tools.set(name, value);
    }
  }

  get tools() {
    return linq.entries(this.#tools).selectAsync(async ([key, value]) => <Tuple<string, string>>[key, await this.resolveAndVerify(value)]);
  }

  async getTool(name: string): Promise<string | undefined> {
    const t = findCaseInsensitiveOnWindows(this.#tools, name);
    if (t) {
      const path = await this.resolveAndVerify(t);
      return await this.validatePath(path) ? path : undefined;
    }
    return undefined;
  }

  /** Aliases are tools that get exposed to the user as shell aliases */
  addAlias(name: string, value: string) {
    const a = findCaseInsensitiveOnWindows(this.#aliases, name);
    if (a === undefined) {
      this.#aliases.set(name, value);
    } else if (a !== value) {
      this.channels.warning(i`Duplicate alias declared ${name} during activation.  New value will replace old.`);
      this.#aliases.set(name, value);
    }
  }

  async getAlias(name: string, refcheck = new Set<string>()): Promise<string | undefined> {
    const v = findCaseInsensitiveOnWindows(this.#aliases, name);
    if (v !== undefined) {
      return this.resolveAndVerify(v, [], refcheck);
    }
    return undefined;
  }

  get aliases() {
    return linq.entries(this.#aliases).selectAsync(async ([key, value]) => <Tuple<string, string>>[key, await this.resolveAndVerify(value)]);
  }

  get aliasCount() {
    return this.#aliases.size;
  }

  /** a collection of 'published locations' from artifacts */
  addLocation(name: string, location: string | Uri) {
    if (!name || !location) {
      return;
    }
    location = typeof location === 'string' ? location : location.fsPath;

    const l = this.#locations.get(name);
    if (l === undefined) {
      this.#locations.set(name, location);
    } else if (l !== location) {
      this.channels.warning(i`Duplicate location declared ${name} during activation. New value will replace old.`);
      this.#locations.set(name, location);
    }
  }

  get locations() {
    return linq.entries(this.#locations).selectAsync(async ([key, value]) => <Tuple<string, string>>[key, await this.resolveAndVerify(value)]);
  }

  getLocation(name: string) {
    const l = this.#locations.get(name);
    return l ? this.resolveAndVerify(l) : undefined;
  }

  /** a collection of environment variables from artifacts that are intended to be combinined into variables that have PATH delimiters */
  addPath(name: string, location: string | Iterable<string> | Uri | Iterable<Uri>) {
    if (!name || !location) {
      return;
    }

    let set = findCaseInsensitiveOnWindows(this.#paths, name);

    if (!set) {
      set = new Set<string>();
      this.#paths.set(name, set);
    }

    if (isIterable(location)) {
      for (const l of location) {
        set.add(typeof l === 'string' ? l : l.fsPath);
      }
    } else {
      set.add(typeof location === 'string' ? location : location.fsPath);
    }
  }

  get paths() {
    return linq.entries(this.#paths).selectAsync(async ([key, value]) => <Tuple<string, Set<string>>>[key, await this.resolveAndVerify(value)]);
  }

  async getPath(name: string) {
    const set = this.#paths.get(name);
    if (!set) {
      return undefined;
    }
    return this.resolveAndVerify(set);
  }

  /** environment variables from artifacts */
  addEnvironmentVariable(name: string, value: string | Iterable<string>) {
    if (!name) {
      return;
    }

    let v = findCaseInsensitiveOnWindows(this.#environmentChanges, name);
    if (!v) {
      v = new Set<string>();
      this.#environmentChanges.set(name, v);
    }

    if (typeof value === 'string') {
      v.add(value);
    } else {
      for (const each of value) {
        v.add(each);
      }
    }
  }

  /** a collection of arbitrary properties from artifacts */
  addProperty(name: string, value: string | Iterable<string>) {
    if (!name) {
      return;
    }
    let v = this.#properties.get(name);
    if (v === undefined) {
      v = new Set<string>();
      this.#properties.set(name, v);
    }

    if (typeof value === 'string') {
      v.add(value);
    } else {
      for (const each of value) {
        v.add(each);
      }
    }
  }

  get properties() {
    return linq.entries(this.#properties).selectAsync(async ([key, value]) => <Tuple<string, Set<string>>>[key, await this.resolveAndVerify(value)]);
  }

  async getProperty(name: string) {
    const v = this.#properties.get(name);
    return v ? await this.resolveAndVerify(v) : undefined;
  }

  msBuildProcessPropertyValue(value: string, targetFolder: Uri) {
    // note that this is intended to be consistent with vcpkg's handling:
    // include/vcpkg/base/api_stable_format.h
    const initialLocal = targetFolder.fsPath;
    const endsWithSlash = initialLocal.endsWith('\\') || initialLocal.endsWith('/');
    const root = endsWithSlash ? initialLocal.substring(0, initialLocal.length - 1) : initialLocal;
    const replacements = new Map<string, string>([['root', root]]);
    return replaceCurlyBraces(value, replacements);
  }

  addMSBuildProperty(name: string, value: string, targetFolder: Uri) {
    this.#msbuild_properties.push([name, this.msBuildProcessPropertyValue(value, targetFolder)]);
  }

  async resolveAndVerify(value: string, locals?: Array<string>, refcheck?: Set<string>): Promise<string>
  async resolveAndVerify(value: Set<string>, locals?: Array<string>, refcheck?: Set<string>): Promise<Set<string>>
  async resolveAndVerify(value: string | Set<string>, locals: Array<string> = [], refcheck = new Set<string>()): Promise<string | Set<string>> {
    if (typeof value === 'string') {
      value = this.resolveVariables(value, locals, refcheck);

      if (value.indexOf('{') === -1) {
        return value;
      }
      const parts = value.split(/\{+(.+?)\}+/g);
      const result = [];
      for (let index = 0; index < parts.length; index += 2) {
        result.push(parts[index]);
        result.push(await this.validatePath(parts[index + 1]));
      }
      return result.join('');
    }
    // for sets
    const result = new Set<string>();
    await new Queue().enqueueMany(value, async (v) => result.add(await this.resolveAndVerify(v, locals))).done;
    return result;
  }

  private resolveVariables(text: string, locals: Array<string> = [], refcheck = new Set<string>()): string {
    if (isScalar(text)) {
      this.channels.debug(`internal warning: scalar value being used directly : ${text.value}`);
      text = <any>text.value; // spews a --debug warning if a scalar makes its way thru for some reason
    }

    // short-circuiting
    if (!text || text.indexOf('$') === -1) {
      return text;
    }

    // prevent circular resolution
    if (refcheck.has(text)) {
      this.channels.warning(i`Circular variable reference detected: ${text}`);
      this.channels.debug(i`Circular variable reference detected: ${text} - ${linq.join(refcheck, ' -> ')}`);
      return text;
    }

    return text.replace(/(\$\$)|(\$)([a-zA-Z_][a-zA-Z0-9_]*)\.([a-zA-Z_][a-zA-Z0-9_]*)|(\$)([a-zA-Z_][a-zA-Z0-9_]*)/g, (_wholeMatch, isDoubleDollar, isObjectMember, obj, member, _isSimple, variable) => {
      return isDoubleDollar ? '$' : isObjectMember ? this.getValueForVariableSubstitution(obj, member, locals, refcheck) : this.resolveVariables(locals[variable], locals, refcheck);
    });
  }

  private getValueForVariableSubstitution(obj: string, member: string, locals: Array<string>, refcheck: Set<string>): string {
    switch (obj) {
      case 'environment': {
        // lookup environment variable value
        const v = findCaseInsensitiveOnWindows(this.#environmentChanges, member);
        if (v) {
          return this.resolveVariables(linq.join(v, ' '), [], refcheck);
        }

        // lookup the environment variable in the original environment
        const orig = this.environment[member];
        if (orig) {
          return orig;
        }
        break;
      }

      case 'defines': {
        const v = findCaseInsensitiveOnWindows(this.#defines, member);
        if (v !== undefined) {
          return this.resolveVariables(v, locals, refcheck);
        }
        break;
      }

      case 'aliases': {
        const v = findCaseInsensitiveOnWindows(this.#aliases, member);
        if (v !== undefined) {
          return this.resolveVariables(v, locals, refcheck);
        }
        break;
      }

      case 'locations': {
        const v = findCaseInsensitiveOnWindows(this.#locations, member);
        if (v !== undefined) {
          return this.resolveVariables(v, locals, refcheck);
        }
        break;
      }

      case 'paths': {
        const v = findCaseInsensitiveOnWindows(this.#paths, member);
        if (v !== undefined) {
          return this.resolveVariables(linq.join(v, delimiter), locals, refcheck);
        }
        break;
      }

      case 'properties': {
        const v = findCaseInsensitiveOnWindows(this.#properties, member);
        if (v !== undefined) {
          return this.resolveVariables(linq.join(v, ';'), locals, refcheck);
        }
        break;
      }

      case 'tools': {
        const v = findCaseInsensitiveOnWindows(this.#tools, member);
        if (v !== undefined) {
          return this.resolveVariables(v, locals, refcheck);
        }
        break;
      }

      default:
        this.channels.warning(i`Variable reference found '$${obj}.${member}' that is referencing an unknown base object.`);
        return `$${obj}.${member}`;
    }

    this.channels.debug(i`Unresolved variable reference found ($${obj}.${member}) during variable substitution.`);
    return `$${obj}.${member}`;
  }


  private async validatePath(path: string) {
    if (path) {
      try {
        if (path[0] === '"') {
          path = path.substr(1, path.length - 2);
        }
        path = resolve(path);
        await lstat(path);

        // if the path has spaces, we need to quote it
        if (path.indexOf(' ') !== -1) {
          path = `"${path}"`;
        }

        return path;
      } catch {
        // does not exist
        this.channels.error(i`Invalid path - does not exist: ${path}`);
      }
    }
    return '';
  }

  expandPathLikeVariableExpressions(value: string): Array<string> {
    let n : number | undefined = undefined;
    const parts = value.split(/(\$[a-zA-Z0-9_.]+)/g).filter(each => each).map((part, i) => {

      const value = this.resolveVariables(part).replace(/\{(.*?)\}/g, (_match, expression) => expression);

      if (value.indexOf(delimiter) !== -1) {
        n = i;
      }

      return value;
    });

    if (n === undefined) {
      // if the value didn't have a path separator, then just return the value
      return [parts.join('')];
    }

    const front = parts.slice(0, n).join('');
    const back = parts.slice(n + 1).join('');

    return parts[n].split(delimiter).filter(each => each).map(each => `${front}${each}${back}`);
  }

  generateMSBuild(): string {
    const result : XmlWriter = new XMLWriterImpl('  ');
    result.startDocument('1.0', 'utf-8');
    result.startElement('Project');
    result.writeAttribute('xmlns', 'http://schemas.microsoft.com/developer/msbuild/2003');
    if (this.#msbuild_properties.length) {
      result.startElement('PropertyGroup');
      for (const [key, value] of this.#msbuild_properties) {
        result.writeElement(key, value);
      }

      result.endElement(); // PropertyGroup
    }

    result.endElement(); // Project
    return result.toString();
  }

  protected async generateEnvironmentVariables(): Promise<[Record<string, string>, Record<string, string>]> {
    const undo : Record<string, string> = {};
    const env : Record<string, string> = {};

    for await (const [pathVariable, locations] of this.paths) {
      if (locations.size) {
        const originalVariable = linq.find(this.environment, pathVariable) || '';
        if (originalVariable) {
          for (const p of originalVariable.split(delimiter)) {
            if (p) {
              locations.add(p);
            }
          }
        }
        // compose the final value
        env[pathVariable] = linq.join(locations, delimiter);

        // set the undo data
        undo[pathVariable] = originalVariable || '';
      }
    }

    // combine environment variables with multiple values with spaces (uses: CFLAGS, etc)
    const environmentVariables = linq.entries(this.#environmentChanges)
      .selectAsync(async ([key, value]) => <Tuple<string, Set<string>>>[key, await this.resolveAndVerify(value)]);
    for await (const [variable, values] of environmentVariables) {
      env[variable] = linq.join(values, ' ');
      undo[variable] = this.environment[variable] || '';
    }

    // .tools get defined as environment variables too.
    for await (const [variable, value] of this.tools) {
      env[variable] = value;
      undo[variable] = this.environment[variable] || '';
    }

    // .defines get compiled into a single environment variable.
    let defines = '';
    for await (const [name, value] of this.defines) {
      defines += value ? `-D${name}=${value} ` : `-D${name} `;
    }

    if (defines) {
      env['DEFINES'] = defines;
      undo['DEFINES'] = this.environment['DEFINES'] || '';
    }

    return [env, undo];
  }

  async activate(thisStackEntries: Array<string>, msbuildFile: Uri | undefined, json: Uri | undefined) : Promise<boolean> {
    const postscriptFile = this.postscriptFile;
    if (!postscriptFile && !msbuildFile && !json) {
      displayNoPostScriptError(this.channels);
      return false;
    }

    async function transformtoRecord<T, U = T> (
      orig: AsyncGenerator<Promise<Tuple<string, T>>, any, unknown>,
      // this type cast to U isn't *technically* correct but since it's locally scoped for this next block of code it shouldn't cause problems
      // eslint-disable-next-line @typescript-eslint/consistent-type-assertions
      func: (value: T) => U = (x => x as unknown as U)) {

      return linq.values((await toArrayAsync(orig))).toObject(tuple => [tuple[0], func(tuple[1])]);
    }

    const defines = await transformtoRecord(this.defines);
    const aliases = await transformtoRecord(this.aliases);
    const locations = await transformtoRecord(this.locations);
    const tools = await transformtoRecord(this.tools);
    const properties = await transformtoRecord(this.properties, (set) => Array.from(set));
    const paths = await transformtoRecord(this.paths, (set) => Array.from(set));

    const [variables, undo] = await this.generateEnvironmentVariables();

    // msbuildFile and json are always generated as if deactivation happend first so that their
    // content does not depend on the stacked environment.
    if (msbuildFile) {
      const contents = await this.generateMSBuild();
      this.channels.debug(`--------[START MSBUILD FILE]--------\n${contents}\n--------[END MSBUILD FILE]---------`);
      await msbuildFile.writeUTF8(contents);
    }

    if (json) {
      const contents = generateJson(variables, defines, aliases, properties, locations, paths, tools);
      this.channels.debug(`--------[START ENV VAR FILE]--------\n${contents}\n--------[END ENV VAR FILE]---------`);
      await json.writeUTF8(contents);
    }

    const newUndoStack = this.undoFile?.stack ?? [];
    Array.prototype.push.apply(newUndoStack, thisStackEntries);
    this.channels.message(i`Activating: ${newUndoStack.join(' + ')}`);

    if (postscriptFile) {
      // preserve undo environment variables for anything this particular activation did not touch
      const oldEnvironment = this.undoFile?.environment;
      if (oldEnvironment) {
        for (const oldUndoKey in oldEnvironment) {
          undo[oldUndoKey] = oldEnvironment[oldUndoKey] ?? '';
          if (!this.allowStacking && variables[oldUndoKey] === undefined) {
            variables[oldUndoKey] = '';
          }
        }
      }

      if (!variables[undoVariableName]) {
        variables[undoVariableName] = this.nextUndoEnvironmentFile.fsPath;
      }

      // if any aliases were undone, remove them
      const oldAliases = this.undoFile?.aliases;
      if (oldAliases) {
        for (const oldAlias in oldAliases) {
          if (aliases[oldAlias] === undefined) {
            aliases[oldAlias] = '';
          }
        }
      }

      // generate shell script
      await writePostscript(this.channels, postscriptFile, variables, aliases);

      const nonEmptyAliases : Array<string> = [];
      for (const alias in aliases) {
        if (aliases[alias]) {
          nonEmptyAliases.push(alias);
        }
      }

      const undoContents : UndoFile = {
        environment: undo,
        aliases: nonEmptyAliases,
        stack: newUndoStack
      };

      const undoStringified = JSON.stringify(undoContents);
      this.channels.debug(`--------[START UNDO FILE]--------\n${undoStringified}\n--------[END UNDO FILE]---------`);
      await this.nextUndoEnvironmentFile.writeUTF8(undoStringified);
    }

    return true;
  }
}

function generateCmdScript(variables: Record<string, string | undefined>, aliases: Record<string, string>): string {
  return linq.entries(variables).select(([k, v]) => { return v ? `set ${k}=${v}` : `set ${k}=`; }).join('\r\n') +
    '\r\n' +
    linq.entries(aliases).select(([k, v]) => { return v ? `doskey ${k}=${v} $*` : `doskey ${k}=`; }).join('\r\n') +
    '\r\n';
}

function generatePowerShellScript(variables: Record<string, string | undefined>, aliases: Record<string, string>): string {
  return linq.entries(variables).select(([k, v]) => { return v ? `$\{ENV:${k}}="${v}"` : `$\{ENV:${k}}=$null`; }).join('\n') +
    '\n' +
    linq.entries(aliases).select(([k, v]) => { return v ? `function global:${k} { & ${v} @args }` : `remove-item -ea 0 "function:${k}"`; }).join('\n') +
    '\n';
}

function generatePosixScript(variables: Record<string, string | undefined>, aliases: Record<string, string>): string {
  return linq.entries(variables).select(([k, v]) => { return v ? `export ${k}="${v}"` : `unset ${k}`; }).join('\n') +
    '\n' +
    linq.entries(aliases).select(([k, v]) => { return v ? `${k}() {\n  ${v} $* \n}` : `unset -f ${v} > /dev/null 2>&1`; }).join('\n') +
    '\n';
}

function generateScriptContent(kind: string, variables: Record<string, string | undefined>, aliases: Record<string, string>) {
  switch (kind) {
    case '.ps1':
      return generatePowerShellScript(variables, aliases);
    case '.cmd':
      return generateCmdScript(variables, aliases);
    case '.sh':
      return generatePosixScript(variables, aliases);
  }
  return '';
}

async function writePostscript(channels: Channels, postscriptFile: Uri, variables: Record<string, string | undefined>, aliases: Record<string, string>) {
  const contents = generateScriptContent(extname(postscriptFile.fsPath), variables, aliases);
  channels.debug(`--------[START SHELL SCRIPT FILE]--------\n${contents}\n--------[END SHELL SCRIPT FILE]---------`);
  channels.debug(`Postscript file ${postscriptFile}`);
  await postscriptFile.writeUTF8(contents);
}

function generateJson(variables: Record<string, string>, defines: Record<string, string>, aliases: Record<string, string>,
  properties:Record<string, Array<string>>, locations: Record<string, string>, paths: Record<string, Array<string>>, tools: Record<string, string>): string {

  let contents = {
    'version': 1,
    variables,
    defines,
    aliases,
    properties,
    locations,
    paths,
    tools
  };

  return JSON.stringify(contents);
}

function printDeactivatingMessage(channels: Channels, stack: Array<string>) {
  channels.message(i`Deactivating: ${stack.join(' + ')}`);
}


export async function deactivate(session: Session, warnIfNoActivation: boolean) : Promise<boolean> {
  const undoVariableValue = process.env[undoVariableName];
  if (!undoVariableValue) {
    if (warnIfNoActivation) {
      session.channels.warning(i`nothing is activated, no changes have been made`);
    }

    return true;
  }

  const postscriptFileName = process.env[postscriptVariable];
  if (!postscriptFileName) {
    displayNoPostScriptError(session.channels);
    return false;
  }

  const postscriptFile = session.fileSystem.file(postscriptFileName);
  const undoFileUri = session.fileSystem.file(undoVariableValue);
  const undoFileRaw = await undoFileUri.tryReadUTF8();
  if (undoFileRaw) {
    const undoFile = <UndoFile>JSON.parse(undoFileRaw);
    const deactivationStack = undoFile.stack;
    if (deactivationStack) {
      printDeactivatingMessage(session.channels, deactivationStack);
    }

    const deactivationEnvironment = {...undoFile.environment};
    deactivationEnvironment[undoVariableName] = '';

    const deactivateAliases : Record<string, string> = {};
    const aliases = undoFile.aliases;
    if (aliases) {
      for (const alias of aliases) {
        deactivateAliases[alias] = '';
      }
    }

    await writePostscript(session.channels, postscriptFile, deactivationEnvironment, deactivateAliases);
    await undoFileUri.delete();
  }

  return true;
}

// replace all values in target with those in source
function undoActivation(target: NodeJS.ProcessEnv, source: Record<string, string | undefined>) {
  for (const key in source) {
    const value = source[key];
    if (value) {
      target[key] = value;
    } else {
      delete target[key];
    }
  }
}

async function toArrayAsync<T>(iterable: AsyncIterable<T>) {
  const result = [];
  for await (const item of iterable) {
    result.push(item);
  }
  return result;
}

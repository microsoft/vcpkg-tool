#!/usr/bin/env node

// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { spawn } from 'child_process';
import { argv } from 'process';
import { CommandLine } from './cli/command-line';
import { AcquireCommand } from './cli/commands/acquire';
import { AcquireProjectCommand } from './cli/commands/acquire-project';
import { ActivateCommand } from './cli/commands/activate';
import { AddCommand } from './cli/commands/add';
import { CacheCommand } from './cli/commands/cache';
import { CleanCommand } from './cli/commands/clean';
import { DeactivateCommand } from './cli/commands/deactivate';
import { DeleteCommand } from './cli/commands/delete';
import { FindCommand } from './cli/commands/find';
import { GenerateMSBuildPropsCommand } from './cli/commands/generate-msbuild-props';
import { HelpCommand } from './cli/commands/help';
import { ListCommand } from './cli/commands/list';
import { RegenerateCommand } from './cli/commands/regenerate-index';
import { RemoveCommand } from './cli/commands/remove';
import { UpdateCommand } from './cli/commands/update';
import { UseCommand } from './cli/commands/use';
import { cli } from './cli/constants';
import { command as formatCommand, hint } from './cli/format';
import { debug, error, initStyling, log } from './cli/styling';
import { i, setLocale } from './i18n';
import { Session } from './session';

// parse the command line
const commandline = new CommandLine(argv.slice(2));

// try to set the locale based on the users's settings.
setLocale(commandline.language);

export let session: Session;
require('./exports');

async function main() {

  // ensure we can execute commands from this process.
  // this works around an odd bug in the way that node handles
  // executing child processes where the target is a windows store symlink
  spawn(process.argv0, ['--version']);

  // create our session for this process.
  session = new Session(process.cwd(), commandline.context, <any>commandline, process.env);

  initStyling(commandline, session);

  // start up the session and init the channel listeners.
  await session.init();

  const help = new HelpCommand(commandline);

  const find = new FindCommand(commandline);
  const list = new ListCommand(commandline);

  const add = new AddCommand(commandline);
  const acquire_project = new AcquireProjectCommand(commandline);
  const acquire = new AcquireCommand(commandline);
  const use = new UseCommand(commandline);

  const remove = new RemoveCommand(commandline);
  const del = new DeleteCommand(commandline);

  const activate = new ActivateCommand(commandline);
  const activate_msbuildprops = new GenerateMSBuildPropsCommand(commandline);
  const deactivate = new DeactivateCommand(commandline);

  const regenerate = new RegenerateCommand(commandline);
  const update = new UpdateCommand(commandline);

  const cache = new CacheCommand(commandline);
  const clean = new CleanCommand(commandline);

  debug(`Postscript file ${session.postscriptFile}`);

  const needsHelp = !!(commandline.switches['help'] || commandline.switches['?'] || (['-h', '-help', '-?', '/?'].find(each => argv.includes(each))));
  // check if --help -h -? --? /? are asked for
  if (needsHelp) {
    // let's just run general help
    await help.run();
    return process.exit(0);
  }

  const command = commandline.command;
  if (!command) {
    // no command recognized.

    // did they specify inputs?
    if (commandline.inputs.length > 0) {
      // unrecognized command
      error(i`Unrecognized command '${commandline.inputs[0]}'`);
      log(hint(i`Use ${formatCommand(`${cli} ${help.command}`)} to get help`));
      return process.exitCode = 1;
    }

    log(hint(i`Use ${formatCommand(`${cli} ${help.command}`)} to get help`));

    return process.exitCode = 0;
  }
  let result = true;
  try {
    result = await command.run();
  } catch (e) {
    // in --debug mode we want to see the stack trace(s).
    if (commandline.debug && e instanceof Error) {
      log(e.stack);
      if (e instanceof AggregateError) {
        e.errors.forEach(each => log(each.stack));
      }
    }

    error(e);

    await session.writeTelemetry();
    return process.exit(result ? 0 : 1);
  } finally {
    await session.writeTelemetry();
  }

  return process.exit(result ? 0 : 1);
}

// eslint-disable-next-line @typescript-eslint/no-floating-promises
main();

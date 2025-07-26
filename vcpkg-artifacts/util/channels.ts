// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { EventEmitter } from 'node:events';
import { Session } from '../session';

/** Event defintions for channel events */
export interface ChannelEvents {
  warning(text: string, msec: number): void;
  error(text: string, msec: number): void;
  message(text: string, msec: number): void;
  debug(text: string, msec: number): void;
}

/**
 * @internal
 *
 * Tracks timing of events
*/
export class Stopwatch {
  start: number;
  last: number;
  constructor() {
    this.last = this.start = process.uptime() * 1000;
  }
  get time() {
    const now = process.uptime() * 1000;
    const result = Math.floor(now - this.last);
    this.last = now;
    return result;
  }
  get total() {
    const now = process.uptime() * 1000;
    return Math.floor(now - this.start);
  }
}

/** Exposes a set of events that are used to communicate with the user
 *
 * Warning, Error, Message, Debug
 */
export class Channels extends EventEmitter {
  /** @internal */
  readonly stopwatch: Stopwatch;

  warning(text: string | Array<string>) {
    if (typeof text === 'string') {
      this.emit('warning', text, this.stopwatch.total);
    } else {
      text.forEach(t => this.emit('warning', t, this.stopwatch.total));
    }
  }
  error(text: string | Array<string>) {
    if (typeof text === 'string') {
      this.emit('error', text, this.stopwatch.total);
    } else {
      text.forEach(t => this.emit('error', t, this.stopwatch.total));
    }
  }
  message(text: string | Array<string>) {
    if (typeof text === 'string') {
      this.emit('message', text, this.stopwatch.total);
    } else {
      text.forEach(t => this.emit('message', t, this.stopwatch.total));
    }
  }
  debug(text: string | Array<string>) {
    if (typeof text === 'string') {
      this.emit('debug', text, this.stopwatch.total);
    } else {
      text.forEach(t => this.emit('debug', t, this.stopwatch.total));
    }
  }
  constructor(session: Session) {
    super();
    this.stopwatch = session.stopwatch;
  }
}

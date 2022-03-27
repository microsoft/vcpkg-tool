// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { EventEmitter } from 'ee-ts';
import { Session } from '../session';

/** Event defintions for channel events */
export interface ChannelEvents {
  warning(text: string, context: any, msec: number): void;
  error(text: string, context: any, msec: number): void;
  message(text: string, context: any, msec: number): void;
  debug(text: string, context: any, msec: number): void;
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
export class Channels extends EventEmitter<ChannelEvents> {
  /** @internal */
  readonly stopwatch: Stopwatch;

  warning(text: string, context?: any) {
    this.emit('warning', text, context, this.stopwatch.total);
  }
  error(text: string, context?: any) {
    this.emit('error', text, context, this.stopwatch.total);
  }
  message(text: string, context?: any) {
    this.emit('message', text, context, this.stopwatch.total);
  }
  debug(text: string, context?: any) {
    this.emit('debug', text, context, this.stopwatch.total);
  }
  constructor(session: Session) {
    super();
    this.stopwatch = session.stopwatch;
  }
}

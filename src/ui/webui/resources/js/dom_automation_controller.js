// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview Extern for window.domAutomationController which is used in
 * browsertests.
 */

/** @constructor */
function DomAutomationController() {}

/** @param {string} json */
DomAutomationController.prototype.send = function(json) {};

/** @type {DomAutomationController} */
window.domAutomationController;

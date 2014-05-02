<?php

/**
 * @file
 * Glue class for running tests on Drush 5/6.
 */

namespace Unish;

abstract class CommandUnishTestCase extends \Drush_CommandTestCase {}

abstract class UnitUnishTestCase extends \Drush_UnitTestCase {}

<?php

/**
 * @file
 * Helper classes for Bandaid.
 */

namespace Bandaid;

/**
 * Bandaid error class.
 */
class BandaidError extends \Exception {
  protected $error;

  /**
   * Constuctor.
   */
  public function __construct($error, $message = '') {
    $this->error = $error;
    parent::__construct($message);
  }

  /**
   * Set error.
   *
   * Calls drush_set_error().
   */
  public function drushSetError() {
    if (($previous = $this->getPrevious()) && ($previous instanceof BandaidError)) {
      $previous->drushSetError();
    }
    // The error has already been set.
    if ($this->error == 'drush_set_error') {
      return FALSE;
    }
    if (!empty($this->message)) {
      return drush_set_error($this->error, $this->message);
    }
    return drush_set_error($this->error);
  }
}

/**
 * Encapsulates Bandaids git operations.
 */
class Git {
  /**
   * Check if a patch will apply.
   */
  public static function applyCheck($filename, $level = 1) {
    return drush_shell_exec('GIT_DIR=. git apply --check -p%s %s --verbose', $level, $filename);
  }

  /**
   * Apply a patch.
   */
  public static function apply($filename, $level = 1) {
    return drush_shell_exec('GIT_DIR=. git apply -p%s %s --verbose', $level, $filename);
  }

  /**
   * Get the oneline log between 2 commits.
   */
  public static function onelineLog($start, $end) {
    if (!drush_shell_exec('git log --format=oneline ' . $start . '..' . $end)) {
      drush_print_r(drush_shell_exec_output());
      throw new BandaidError('CANNOT_GET_HISTORY');
    }
    return drush_shell_exec_output();
  }

  /**
   * Checkout branch/tag/sha.
   */
  public static function checkout($ref) {
    if (!drush_shell_exec('git checkout %s', $ref)) {
      throw new BandaidError('CANNOT_CHECKOUT');
    }
  }

  /**
   * Create a branch.
   */
  public static function branch($name) {
    if (!drush_shell_exec('git branch %s', $name)) {
      throw new BandaidError('CANNOT_BRANCH');
    }
  }

  /**
   * Add files to index.
   */
  public static function add($pattern, $all = FALSE) {
    $all = $all ? '--all' : '';
    if (!drush_shell_exec('git add %s %s', $all, $pattern)) {
      throw new BandaidError('CANNOT_ADD');
    }
  }

  /**
   * Commit changes.
   */
  public static function commit($message) {
    if (!drush_shell_exec('git commit -m %s', drush_escapeshellarg($message))) {
      throw new BandaidError('CANNOT_COMMIT');
    }
  }

  /**
   * Return a diff.
   */
  public static function diff($ref1, $ref2 = '') {
    if (!drush_shell_exec('git diff %s %s', $ref1, $ref2)) {
      throw new BandaidError('CANNOT_DIFF');
    }
    // Due to going through drush_shell_exec_output(), we lost the trailing
    // newline.
    $patch = implode("\n", drush_shell_exec_output());
    return empty($patch) ? '' : $patch . "\n";
  }

  /**
   * Reset workspace.
   */
  public static function reset($hard = FALSE) {
    $hard = $hard ? '--hard' : '';
    if (!drush_shell_exec('git reset %s', $hard)) {
      throw new BandaidError('CANNOT_RESET');
    }
  }

  /**
   * Clone repository.
   */
  public static function cloneRepository($repository, $path, $options = array()) {
    $opt = array();
    foreach ($options as $name => $value) {
      if ($value === TRUE || $value === NULL) {
        $opt[] = '--' . $name;
      }
      else {
        $opt[] = '--' . $name . ' ' . drush_escapeshellarg($value);
      }
    }
    $command = 'git clone ' . implode(' ', $opt) . ' %s %s';
    if (!drush_shell_exec($command, $repository, $path)) {
      throw new BandaidError('CANNOT_CLONE', dt('Unable to clone from @url.', array('@url' => $url)));
    }
  }

  /**
   * Run remote update.
   */
  public static function remoteUpdate() {
    if (!drush_shell_exec('git remote update')) {
      throw new BandaidError('ERROR_UPDATING');
    }
  }
}

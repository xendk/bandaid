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
  public function __construct($error, $message = '', $previous = NULL) {
    $this->error = $error;
    parent::__construct($message, 0, $previous);
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
    $message = !empty($this->message) ? $this->message : NULL;
    return drush_set_error($this->error, $message);
  }
}

/**
 * Encapsulates Bandaids git operations.
 */
class Git {
  /**
   * Run a command.
   */
  protected static function exec($command, $args = array(), $error_code = 'GIT_EXEC_ERROR', $error_msg = "Error executing command \"!command\"") {
    array_unshift($args, $command);
    $result = call_user_func_array('drush_shell_exec', $args);
    array_shift($args);
    if (!$result) {
      throw new BandaidError($error_code, strtr($error_msg, array('!command' => vsprintf($command, $args))));
    }
    return drush_shell_exec_output();
  }

  /**
   * Get the sha for the ref or HEAD.
   */
  public static function sha($ref = 'HEAD') {
    $sha = static::exec('git rev-parse %s', array($ref));
    return trim($sha[0]);
  }

  /**
   * Check if a patch will apply.
   *
   * @return bool
   *   Whether git thinks it will apply.
   */
  public static function applyCheck($filename, $level = 1) {
    try {
      static::exec('GIT_DIR=. git apply --check -p%s %s --verbose', array($level, $filename));
      return TRUE;
    }
    catch (BandaidError $e) {
      return FALSE;
    }
  }

  /**
   * Apply a patch.
   *
   * @return bool
   *   TRUE if the patch was applied successfully.
   */
  public static function apply($filename, $level = 1) {
    try {
      static::exec('GIT_DIR=. git apply -p%s %s --verbose', array($level, $filename));
      return TRUE;
    }
    catch (BandaidError $e) {
      return FALSE;
    }
  }

  /**
   * Get the oneline log for one or more commits.
   */
  public static function onelineLog($start, $end = NULL) {
    if (empty($end)) {
      return static::exec('git log --format=oneline -1 %s', array($start), 'CANNOT_GET_HISTORY');
    }
    else {
      return static::exec('git log --format=oneline %s..%s', array($start, $end), 'CANNOT_GET_HISTORY');
    }
  }

  /**
   * Checkout branch/tag/sha.
   */
  public static function checkout($ref) {
    return static::exec('git checkout %s', array($ref), 'CANNOT_CHECKOUT');
  }

  /**
   * Checkout branch/tag/sha to a work-tree.
   */
  public static function checkoutTo($work_tree, $ref) {
    return static::exec('git --work-tree=%s checkout %s', array($work_tree, $ref), 'CANNOT_CHECKOUT');
  }

  /**
   * Create a branch.
   */
  public static function branch($name) {
    return static::exec('git branch %s', array($name), 'CANNOT_BRANCH');
  }

  /**
   * Add files to index.
   */
  public static function add($pattern, $all = FALSE) {
    $all = $all ? '--all' : '';
    return static::exec('git add %s %s', array($all, $pattern), 'CANNOT_ADD');
  }

  /**
   * Add files to index, from another work-tree.
   */
  public static function addFrom($work_tree, $pattern, $all = FALSE) {
    $all = $all ? '--all' : '';
    return static::exec('git --work-tree=%s add %s %s', array($work_tree, $all, $pattern), 'CANNOT_ADD');
  }

  /**
   * Commit changes.
   */
  public static function commit($message) {
    return static::exec('git commit --allow-empty -m %s', array($message), 'CANNOT_COMMIT');
  }

  /**
   * Return a diff.
   */
  public static function diff($ref1, $ref2 = '') {
    // Just grabbing it from stdout doesn't work as drush <= 6 uses exec, which
    // strips trailing whitespace. Using a temp file gets around that problem.
    $temp_file = drush_tempnam('bandaid_diff_');
    static::exec('git diff %s %s > %s', array($ref1, $ref2, $temp_file), 'CANNOT_DIFF');
    return file_get_contents($temp_file);
  }

  /**
   * Reset workspace.
   *
   * @todo fix up the signature.
   */
  public static function reset($hard = FALSE, $path = '') {
    $hard = $hard ? '--hard' : '';
    // The double dash is rather important.
    return static::exec('git reset %s -- %s', array($hard, $path), 'CANNOT_RESET');
  }

  /**
   * Reset workspace in another work-tree.
   *
   * @todo fix up the signature.
   */
  public static function resetTo($work_tree, $hard = FALSE, $path = '') {
    $hard = $hard ? '--hard' : '';
    // The double dash is rather important.
    return static::exec('git --work-tree=%s reset %s -- %s', array($work_tree, $hard, $path), 'CANNOT_RESET');
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
    return static::exec($command, array($repository, $path), 'CANNOT_CLONE', dt('Unable to clone from @url.', array('@url' => $repository)));
  }

  /**
   * Run remote update.
   */
  public static function remoteUpdate() {
    static::exec('git remote update', array(), 'ERROR_UPDATING');
  }

  /**
   * Add ignores to repository.
   */
  public static function addIgnores($ignores) {
    $exclude_file = '.git/info/exclude';
    $existing_ignores = explode("\n", file_get_contents($exclude_file));
    $ignores = array_merge($existing_ignores, $ignores);
    array_unique($ignores);
    file_put_contents($exclude_file, implode("\n", $ignores));
  }
}

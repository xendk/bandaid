<?php

/**
 * @file
 * PHPUnit Tests for Bandaid.
 */

/**
 * Deployotron testing class.
 */
class DeployotronCase extends Drush_CommandTestCase {
  /**
   * Setup before running any tests.
   */
  public static function setUpBeforeClass() {
    parent::setUpBeforeClass();
    // Copy in the command file, so the sandbox can find it.
    $drush_dir = getenv('HOME') . '/.drush';
    exec('cp -r ' . escapeshellarg(dirname(dirname(__FILE__))) . ' ' . escapeshellarg($drush_dir));
    // Need to set up git minimally for it to work (else it wont commit).
    exec('git config --global user.email "drush@example.com"');
    exec('git config --global user.name "Bandaid Test cases"');
  }

  /**
   * Setup before each test case.
   */
  public function setUp() {
    // Deployotron needs a site to run in.
    if (file_exists($this->webroot())) {
      exec("rm -rf " . $this->webroot() . '/sites/*');
    }
    $this->setUpDrupal(1);
  }

  /**
   * Test the basic patch, tearoff, upgrade, apply cycle.
   */
  public function testBasicFunctionallity() {
    $workdir = $this->webroot() . '/sites/all/modules';
    $this->drush('dl', array('panels-3.3'), array(), NULL, $workdir);

    // Apply first patch, and check for success.
    $options = array(
      'home' => 'https://drupal.org/node/1985980',
      'reason' => 'For altering of new panes.',
    );
    $patch1_string = 'drupal_alter(\'panels_new_pane\', $pane);';
    $this->assertEmpty($this->grep($patch1_string, $workdir . '/panels'));
    $this->drush('bandaid-patch', array('https://drupal.org/files/issues/panels-new-pane-alter-1985980-5.patch', 'panels'), $options, NULL, $workdir);
    $this->assertNotEmpty($this->grep($patch1_string, $workdir . '/panels'));

    $options = array(
      'home' => 'https://drupal.org/node/2098515',
      'reason' => 'To avoid notice.',
    );
    $patch2_string = 'if (!isset($pane->type)) {';
    $this->assertEmpty($this->grep($patch2_string, $workdir . '/panels'));
    $this->drush('bandaid-patch', array('https://drupal.org/files/issues/undefined_property_notices_fix-2098515-2.patch', 'panels'), $options, NULL, $workdir);
    $this->assertNotEmpty($this->grep($patch2_string, $workdir . '/panels'));

    // Tearoff the patches and check that they're gone.
    $this->drush('bandaid-tearoff', array('panels'), array(), NULL, $workdir);
    $this->assertEmpty($this->grep($patch1_string, $workdir . '/panels'));
    $this->assertEmpty($this->grep($patch2_string, $workdir . '/panels'));

  }

  /**
   * Grep for a string.
   */
  protected function grep($string, $root) {
    exec('grep -r ' . escapeshellarg($string) . ' ' . escapeshellarg($root), $output, $rc);
    if ($rc > 1) {
      $this->fail("Error grepping.");
    }
    return implode("\n", $output);
  }
}

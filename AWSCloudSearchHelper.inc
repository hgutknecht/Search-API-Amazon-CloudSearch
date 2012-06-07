<?php


/**
 * @file
 * aws_helpder.inc
 * Simple helper class for use with CloudSearch. 
 *  
*/

require_once 'AWSSDKforPHP/sdk.class.php';
require_once 'AWSSDKforPHP/services/ec2.class.php';
require_once 'AWSSDKforPHP/services/cloudsearch.class.php';

class AWSCloudSearchHelper {
  
  /*
   * Creates an instance of the AmazonCloudSearch class given security credentials and AWS region.
   * 
   */
  public static function getAWSCloudSearchInstance($key, $secret, $region) {
    $search = new AmazonCloudSearch(self::buildAWSCredentials($key, $secret));
    $search->set_region($region);
    return $search;
  }
  
  
  /*
   * Builds the credential array required by the AWS API
   * 
   * @todo Possibly look at softening the default_cache_config setting 
   */
  public static function buildAWSCredentials($key, $secret) {
    $credentials = array();
    $credentials['certificate_authority'] = FALSE;
    $credentials['key'] =  $key;
    $credentials['secret'] = $secret;
    $credentials['default_cache_config'] = 'APC';
    return $credentials;
  }  
  
 
  /**
   * Gets all applicable regions for AWS EC2 instances.  At present this is the only way
   * to get a list of regions via the API.
   */  
  public static function getAWSRegions($key, $secret) {
    //have to use the EC2 class for this.
    $ec2 = new AmazonEC2(buildAWSCredentials($key, $secret));
    $response = $ec2->describe_regions();
    if (!$response->isOK()) {
      return NULL;
    }
    return $response->body->regionInfo;
  }

  /**
   * Given a response from the AWS API will check if it has an error message and returns it.
   */  
  public static function getAWSError($response) {
    if (!isset($response->body->Error->Message)) {
      return t('Failed to get AWS error message');
    }
    else {
      return $response->body->Error->Message;      
    }
  }
  
  /**
   * Check the CloudSearch response and log any errors, returning true if OK and false if error
   */
  public static function checkAndReport($context, $response, $throw_exception = FALSE) {
    if (!$response->isOK()) {
      $aws_msg = self::getAWSError($response);
      $msg = t('CloudSearch request failed:') . ' ' . t('Context') . ': ' . $context . ', ' . t('Message') . ': ' . $aws_msg;
      drupal_set_message(check_plain($msg), 'error');
      watchdog('aws_cloudsearch', $msg, NULL, WATCHDOG_CRITICAL, $link = NULL);
      if ($throw_exception) {
        throw new Exception($msg);
      }
      return FALSE;
    }
    else {
      return TRUE;
    }
  }

}
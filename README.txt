Search API CloudSearch
--------------------------

Offers an implementation of the Search API that uses an Amazon 
AWS CloudSearch server for indexing content.

Prerequisites
---------------

The Amazon AWS SDK for PHP needs to be installed. 

Via PEAR (recommended)

From the command-line, you can install the SDK with PEAR as 
follows (assumes LINUX):

> pear channel-discover pear.amazonwebservices.com
> pear install aws/sdk

Via GitHub

Pull down the source code down into a directory under the sites/all/libraries/awssdk

> git clone git://github.com/amazonwebservices/aws-sdk-for-php.git AWSSDKforPHP

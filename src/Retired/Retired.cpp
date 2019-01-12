bool DownloadOrphanImage(std::string sBaseURL, std::string sPage, std::string sTargetFile, std::string& sError)
{
	std::string sPath2 = GetSANDirectory2() + sTargetFile;
    boost::filesystem::remove(sPath2);

	int iMaxSize = 900000000;
    int iTimeoutSecs = 60 * 7;
	LogPrintf("Downloading Orphan Image NAME %s FROM URL %s ", sPath2.c_str(), sBaseURL.c_str());
	int iIterations = 0;
	try
	{
		map<string, string> mapRequestHeaders;
		mapRequestHeaders["Agent"] = FormatFullVersion();
		BIO* bio;
		SSL_CTX* ctx;
		SSL_library_init();
		ctx = SSL_CTX_new(SSLv23_client_method());
		if (ctx == NULL)
		{
			sError = "<ERROR>CTX_IS_NULL</ERROR>";
			fDistributedComputingCycleDownloading = false;
			return false;
		}
		bio = BIO_new_ssl_connect(ctx);
		std::string sDomain = GetDomainFromURL(sBaseURL);
		std::string sDomainWithPort = sDomain + ":" + "443";
		BIO_set_conn_hostname(bio, sDomainWithPort.c_str());
		if(BIO_do_connect(bio) <= 0)
		{
			sError = "Failed connection to " + sDomainWithPort;
			fDistributedComputingCycleDownloading = false;
			return false;
		}
		CService addrConnect;
		if (sDomain.empty()) 
		{
			sError = "DOMAIN_MISSING";
			fDistributedComputingCycleDownloading = false;
			return false;
		}
		CService addrIP(sDomain, 443, true);
    	if (addrIP.IsValid())
		{
			addrConnect = addrIP;
		}
		else
		{
  			sError = "<ERROR>DNS_ERROR</ERROR>"; 
			fDistributedComputingCycleDownloading = false;
			return false;
		}
		std::string sPayload = "";
		std::string sPost = PrepareHTTPPost(true, sPage, sDomain, sPayload, mapRequestHeaders);
		const char* write_buf = sPost.c_str();
		if(BIO_write(bio, write_buf, strlen(write_buf)) <= 0)
		{
			sError = "<ERROR>FAILED_HTTPS_POST</ERROR>";
			fDistributedComputingCycleDownloading = false;
			return false;
		}
		int iSize;
		int iBufSize = 1024;
		clock_t begin = clock();
		std::string sData = "";
		char bigbuf[iBufSize];
	
		FILE *outUserFile = fopen(sPath2.c_str(), "wb");
		for(;;)
		{
	
			iSize = BIO_read(bio, bigbuf, iBufSize);
			bool fShouldRetry = BIO_should_retry(bio);

			if(iSize <= 0 && !fShouldRetry)
			{
				LogPrintf("DCC download finished \n");
				break;
			}

			size_t bytesWritten=0;
			if (iSize > 0)
			{
				if (iIterations == 0)
				{
					// GZ magic bytes: 31 139
					int iPos = 0;
					for (iPos = 0; iPos < iBufSize; iPos++)
					{
						if (bigbuf[iPos] == 31) if (bigbuf[iPos + 1] == (char)0x8b)
						{
							break;
						}
					}
					int iNewSize = iBufSize - iPos;
					char smallbuf[iNewSize];
					for (int i=0; i < iNewSize; i++)
					{
						smallbuf[i] = bigbuf[i + iPos];
					}
					bytesWritten = fwrite(smallbuf, 1, iNewSize, outUserFile);
				}
				else
				{
					bytesWritten = fwrite(bigbuf, 1, iSize, outUserFile);
				}
				iIterations++;
			}
			clock_t end = clock();
			double elapsed_secs = double(end - begin) / (CLOCKS_PER_SEC + .01);
			if (elapsed_secs > iTimeoutSecs) 
			{
				LogPrintf(" download timed out ... (bytes written %f)  \n", (double)bytesWritten);
				break;
			}
			if (false && (int)sData.size() >= iMaxSize) 
			{
				LogPrintf(" download oversized .. \n");
				break;
			}
		}
		// R ANDREW - JAN 4 2018: Free bio resources
		BIO_free_all(bio);
	 	fclose(outUserFile);
	}
	catch (std::exception &e)
	{
        sError = "<ERROR>WEB_EXCEPTION</ERROR>";
		return false;
    }
	catch (...)
	{
		sError = "<ERROR>GENERAL_WEB_EXCEPTION</ERROR>";
		return false;
	}
	return true;
}
